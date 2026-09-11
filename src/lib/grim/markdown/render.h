#pragma once

#include <lib/grim/bp.h>
#include <lib/grim/gfx/internal_graphics.h>
#include <lib/grim/markdown/slug.h>
#include <stdalign.h>

typedef struct TextPushConstants {
    VkDeviceAddress window, font;
    Vec2 scroll_offset, cursor_position;
} TextPushConstants;
STATIC_ASSERT(sizeof(TextPushConstants) == 32);
STATIC_ASSERT(offsetof(TextPushConstants, scroll_offset) == 16);
STATIC_ASSERT(offsetof(TextPushConstants, cursor_position) == 24);

typedef struct GlyphDrawCmd {
    f32 x, y, sx, sy;
    u32 glyph_idx, color;
} GlyphDrawCmd;
STATIC_ASSERT(sizeof(GlyphDrawCmd) == 24);

#define GLYPH_INDEX_MASK 0x00ffffffu
#define GLYPH_STYLE_BOLD 0x80000000u
#define GLYPH_STYLE_ITALIC 0x40000000u
#define GPU_RECT_FIXED 1u

typedef struct GpuRect {
    f32 x, y, width, height;
    u32 color, flags;
} GpuRect;
STATIC_ASSERT(sizeof(GpuRect) == 24);

typedef struct TextFrameStyle {
    Vec2 viewport_scale;
    Vec4 text_color;
    Vec2 cursor_size;
    u32 cursor_color;
    bool cursor_visible;
} TextFrameStyle;

// Explicit std430 ABI. Addresses have eight-byte alignment; the window's
// vec4 begins at 32. No bool or native pointer crosses the GPU boundary.
typedef struct GpuWindowHeader {
    alignas(16) VkDeviceAddress glyphs;
    VkDeviceAddress rects;
    u32 glyph_count, rect_count;
    Vec2 viewport_scale;
    Vec4 text_color;
    Vec2 cursor_size;
    u32 cursor_color, cursor_visible;
} GpuWindowHeader;
STATIC_ASSERT(sizeof(GpuWindowHeader) == 64);
STATIC_ASSERT(offsetof(GpuWindowHeader, glyph_count) == 16);
STATIC_ASSERT(offsetof(GpuWindowHeader, viewport_scale) == 24);
STATIC_ASSERT(offsetof(GpuWindowHeader, text_color) == 32);
STATIC_ASSERT(offsetof(GpuWindowHeader, cursor_color) == 56);

typedef struct GpuFontHeader {
    VkDeviceAddress glyphs, curves, bands;
    u32 glyph_count, curve_count, band_words, reserved;
} GpuFontHeader;
STATIC_ASSERT(sizeof(GpuFontHeader) == 40);
STATIC_ASSERT(offsetof(GpuFontHeader, glyph_count) == 24);

// Scalar std430 layout: immutable bounds/transform and two packed uints.
typedef struct SlugGlyphData {
    f32 min_x, min_y, max_x, max_y;
    f32 band_scale_x, band_scale_y, band_offset_x, band_offset_y;
    u32 band_origin, band_counts;
} SlugGlyphData;
STATIC_ASSERT(sizeof(SlugGlyphData) == 40);
STATIC_ASSERT(offsetof(SlugGlyphData, band_origin) == 32);

typedef struct TextDrawSlot {
    Buffer buffer, rect_buffer, header_buffer;
    GpuWindowHeader header;
    u64 revision;
    u32 count, capacity, rect_capacity;
    bool initialized, header_initialized;
} TextDrawSlot;

typedef struct TextRenderState {
    VkQueryPool timestamp_pool; // Owned/read by the application after its slot fence.
    u32 timestamp_base, max_draws;
    // Vulkan allocation requirements, not payload sizes. staging_bytes is live
    // staging allocation and becomes zero when its uploading slot retires.
    u64 asset_bytes, draw_buffer_bytes, staging_bytes, last_upload_bytes;
    // Peak requested preprocessing payload, excluding the mmap-backed font,
    // Vulkan allocations counted above, and allocator/driver overhead.
    u64 construction_bytes;
    Pipeline pipeline;
    Buffer font_buffer, staging;
    TextDrawSlot *slots;
    u32 slot_count, glyph_count;
    u64 curve_bytes, band_bytes, glyph_bytes, upload_frame;
    bool upload_pending;
} TextRenderState;

internal Buffer text_mapped_buffer(const GraphicsContext *ctx, u64 bytes) {
    return buffer_create(ctx, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

internal void text_render_init(Arena *arena, const RenderContext *rc,
                               const FontState *font_state, TextRenderState *trs) {
    *trs = (TextRenderState){.max_draws = 64 * 1024,
        .glyph_count = font_state->glyph_count, .slot_count = rc->frames_in_flight,
        .upload_pending = true};
    const GraphicsContext *ctx = rc->ctx;
    tt_require(trs->slot_count && trs->glyph_count <= GLYPH_INDEX_MASK + 1,
               "invalid text renderer capacity");
    Arena work = arena_create(), scratch = arena_create();
    SlugCtx slug;
    slug_ctx_init(&work, &slug, trs->glyph_count);
    for (u32 gi = 0; gi < trs->glyph_count; gi++) {
        slug_preprocess_glyph(&scratch, &slug, font_state->font, gi);
        trs->construction_bytes = max(trs->construction_bytes, (u64)work.len + scratch.len +
            (u64)slug.curve_capacity * 4 + (u64)slug.band_capacity * 4);
        scratch.len = 0;
    }
    tt_release_data(font_state->font);
    arena_destroy(&scratch);
    SlugGlyphData *glyphs = arena_alloc_aligned(&work, SlugGlyphData, trs->glyph_count);
    for (u32 gi = 0; gi < trs->glyph_count; gi++) {
        SlugGlyphMeta meta = slug.meta[gi];
        glyphs[gi] = (SlugGlyphData){0};
        if (!meta.hband_n) continue;
        f32 lo_x = slug.glyph_bbox_min_x[gi], lo_y = slug.glyph_bbox_min_y[gi];
        f32 hi_x = slug.glyph_bbox_max_x[gi], hi_y = slug.glyph_bbox_max_y[gi];
        f32 sx = meta.vband_n / (hi_x - lo_x + SLUG_BAND_EPS);
        f32 sy = meta.hband_n / (hi_y - lo_y + SLUG_BAND_EPS);
        glyphs[gi] = (SlugGlyphData){lo_x, lo_y, hi_x, hi_y,
            sx, sy, -lo_x * sx, -lo_y * sy,
            meta.band_origin, meta.vband_n | (meta.hband_n << 16)};
    }
    trs->curve_bytes = (u64)slug.curve_count * 12;
    trs->band_bytes = (u64)slug.band_words * 4;
    trs->glyph_bytes = (u64)trs->glyph_count * sizeof(SlugGlyphData);
    u64 glyph_offset = 48;
    u64 curve_offset = (glyph_offset + trs->glyph_bytes + 15) & ~(u64)15;
    u64 band_offset = (curve_offset + trs->curve_bytes + 15) & ~(u64)15;
    u64 bytes = (band_offset + trs->band_bytes + 15) & ~(u64)15;
    trs->font_buffer = buffer_create(ctx, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    trs->asset_bytes = trs->font_buffer.allocation_size;
    GpuFontHeader font = {
        .glyphs = trs->font_buffer.address + glyph_offset,
        .curves = trs->font_buffer.address + curve_offset,
        .bands = trs->font_buffer.address + band_offset,
        .glyph_count = trs->glyph_count, .curve_count = slug.curve_count,
        .band_words = slug.band_words};
    trs->staging = buffer_create(ctx, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    trs->staging_bytes = trs->staging.allocation_size;
    memory_zero(trs->staging.mapped, (usize)bytes);
    buffer_host_copy(ctx, &trs->staging, &font, sizeof(font), 0);
    buffer_host_copy(ctx, &trs->staging, glyphs, trs->glyph_bytes, glyph_offset);
    buffer_host_copy(ctx, &trs->staging, slug.curves, trs->curve_bytes, curve_offset);
    buffer_host_copy(ctx, &trs->staging, slug.bands, trs->band_bytes, band_offset);
    trs->construction_bytes = max(trs->construction_bytes, (u64)work.len +
        (u64)slug.curve_capacity * 4 + (u64)slug.band_capacity * 4);
    free(slug.curves); free(slug.bands);
    arena_destroy(&work);

    trs->slots = arena_alloc_aligned(arena, TextDrawSlot, trs->slot_count);
    for (u32 i = 0; i < trs->slot_count; i++) {
        TextDrawSlot *slot = &trs->slots[i];
        *slot = (TextDrawSlot){.capacity = trs->max_draws, .rect_capacity = 256};
        slot->buffer = text_mapped_buffer(ctx, (u64)slot->capacity * sizeof(GlyphDrawCmd));
        slot->rect_buffer = text_mapped_buffer(ctx, (u64)slot->rect_capacity * sizeof(GpuRect));
        slot->header_buffer = text_mapped_buffer(ctx, sizeof(GpuWindowHeader));
        trs->draw_buffer_bytes += slot->buffer.allocation_size +
            slot->rect_buffer.allocation_size + slot->header_buffer.allocation_size;
    }
    VkPushConstantRange push = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TextPushConstants)};
    VkPipelineLayoutCreateInfo pipeline_layout = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeline_layout.pushConstantRangeCount = 1; pipeline_layout.pPushConstantRanges = &push;
    check_vkresult(vkCreatePipelineLayout(ctx->device, &pipeline_layout, NULL, &trs->pipeline.layout), SCOPE_GFX_PIPELINE, "Slug pipeline layout");
    // Shader loading uses temporary storage, never the persistent app arena.
    Arena shaders = arena_create();
    VkShaderModule vertex = read_shader(&shaders, ctx, "assets/shaders/text_slug.vert.spv");
    VkShaderModule fragment = read_shader(&shaders, ctx, "assets/shaders/text_slug.frag.spv");
    VkPipelineShaderStageCreateInfo stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertex, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragment, .pName = "main"}};
    VkPipelineVertexInputStateCreateInfo input = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    VkPipelineViewportStateCreateInfo viewport = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL; raster.lineWidth = 1.0f;
    raster.cullMode = VK_CULL_MODE_NONE; raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    VkPipelineMultisampleStateCreateInfo samples = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend = {.blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA, .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD, .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo color = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    color.attachmentCount = 1; color.pAttachments = &blend;
    VkDynamicState states[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2; dynamic.pDynamicStates = states;
    VkPipelineRenderingCreateInfo rendering = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1; rendering.pColorAttachmentFormats = &rc->target->format;
    VkGraphicsPipelineCreateInfo pipeline = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline.pNext = &rendering; pipeline.stageCount = 2; pipeline.pStages = stages;
    pipeline.pVertexInputState = &input; pipeline.pInputAssemblyState = &assembly;
    pipeline.pViewportState = &viewport; pipeline.pRasterizationState = &raster;
    pipeline.pMultisampleState = &samples; pipeline.pColorBlendState = &color;
    pipeline.pDynamicState = &dynamic; pipeline.layout = trs->pipeline.layout;
    check_vkresult(vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 1, &pipeline, NULL, &trs->pipeline.handle), SCOPE_GFX_PIPELINE, "Slug pipeline");
    vkDestroyShaderModule(ctx->device, vertex, NULL); vkDestroyShaderModule(ctx->device, fragment, NULL);
    arena_destroy(&shaders);
}

internal void text_upload_assets(VkCommandBuffer cmd, TextRenderState *trs) {
    VkBufferCopy copy = {0, 0, trs->font_buffer.size};
    vkCmdCopyBuffer(cmd, trs->staging.handle, trs->font_buffer.handle, 1, &copy);
    VkBufferMemoryBarrier2 buffer = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT, .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = trs->font_buffer.handle, .size = VK_WHOLE_SIZE};
    VkDependencyInfo dep = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &buffer};
    vkCmdPipelineBarrier2(cmd, &dep);
}

// Called only after this frame slot's fence. Allocate before retiring the
// old address; publish the replacement in its header before submission.
internal bool text_grow_slot_buffer(const GraphicsContext *ctx, TextRenderState *trs,
                                   Buffer *buffer, u32 *capacity, u32 count, u32 stride) {
    if (count <= *capacity) return false;
    u32 cap = *capacity;
    while (cap < count) cap = cap > UINT32_MAX / 2 ? count : cap * 2;
    Buffer replacement = text_mapped_buffer(ctx, (u64)cap * stride);
    trs->draw_buffer_bytes -= buffer->allocation_size;
    cleanup_buffer(ctx, buffer);
    *buffer = replacement;
    *capacity = cap;
    trs->draw_buffer_bytes += replacement.allocation_size;
    return true;
}

// Must follow successful start_frame: its fence protects this slot's mapped
// writes and allocations. A revision identifies immutable glyph contents.
internal void text_render_frame(RenderState *rs, TextRenderState *trs,
    const GlyphDrawCmd *draws, u32 count, const GpuRect *rects, u32 rect_count,
    TextFrameStyle style, TextPushConstants pc, u64 draw_revision) {
    const GraphicsContext *ctx = rs->r_ctx->ctx;
    RenderTarget *target = rs->r_ctx->target;
    u32 slot_index = rs->frame_count % trs->slot_count;
    TextDrawSlot *slot = &trs->slots[slot_index];
    tt_require((u64)count + rect_count + (style.cursor_visible ? 1u : 0u) <= INT32_MAX,
               "text frame instance count overflow");
    tt_require((!count || draws) && (!rect_count || rects), "missing text frame data");
    if (text_grow_slot_buffer(ctx, trs, &slot->buffer, &slot->capacity, count, sizeof(*draws)))
        slot->initialized = false;
    text_grow_slot_buffer(ctx, trs, &slot->rect_buffer, &slot->rect_capacity, rect_count, sizeof(*rects));
    trs->max_draws = max(trs->max_draws, slot->capacity);
    if (trs->staging.handle && !trs->upload_pending && rs->frame_count != trs->upload_frame &&
        slot_index == trs->upload_frame % trs->slot_count) {
        cleanup_buffer(ctx, &trs->staging); trs->staging_bytes = 0;
    }
    trs->last_upload_bytes = 0;
    if (!slot->initialized || slot->revision != draw_revision || slot->count != count) {
        for (u32 i = 0; i < count; i++)
            tt_require((draws[i].glyph_idx & GLYPH_INDEX_MASK) < trs->glyph_count, "invalid glyph cache index");
        trs->last_upload_bytes = (u64)count * sizeof(*draws);
        if (count) buffer_host_copy(ctx, &slot->buffer, draws, trs->last_upload_bytes, 0);
        slot->revision = draw_revision; slot->count = count; slot->initialized = true;
    }
    if (rect_count) {
        u64 bytes = (u64)rect_count * sizeof(*rects);
        buffer_host_copy(ctx, &slot->rect_buffer, rects, bytes, 0);
        trs->last_upload_bytes += bytes;
    }
    GpuWindowHeader header = {
        .glyphs = slot->buffer.address, .rects = slot->rect_buffer.address,
        .glyph_count = count, .rect_count = rect_count,
        .viewport_scale = style.viewport_scale, .text_color = style.text_color,
        .cursor_size = style.cursor_size, .cursor_color = style.cursor_color,
        .cursor_visible = style.cursor_visible ? 1u : 0u};
    if (!slot->header_initialized || memcmp(&header, &slot->header, sizeof(header))) {
        buffer_host_copy(ctx, &slot->header_buffer, &header, sizeof(header), 0);
        slot->header = header; slot->header_initialized = true;
        trs->last_upload_bytes += sizeof(header);
    }
    pc.window = slot->header_buffer.address;
    pc.font = trs->font_buffer.address;
    // Coherent writes happen before queue submission, which performs the host
    // memory-domain operation. No transfer commands or inter-slot barrier.
    VkCommandBuffer cmd = render_command_buffer(rs);
    VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    check_vkresult(vkBeginCommandBuffer(cmd, &begin), SCOPE_GFX_COMMAND_BUFFER, "Begin text frame");
    if (trs->timestamp_pool) {
        vkCmdResetQueryPool(cmd, trs->timestamp_pool, trs->timestamp_base, 2);
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, trs->timestamp_pool, trs->timestamp_base);
    }
    if (trs->upload_pending) {
        text_upload_assets(cmd, trs); trs->upload_pending = false; trs->upload_frame = rs->frame_count;
    }
    VkViewport viewport = {0, 0, (f32)target->extent.width, (f32)target->extent.height, 0, 1};
    VkRect2D scissor = {{0, 0}, target->extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport); vkCmdSetScissor(cmd, 0, 1, &scissor);
    VkImageMemoryBarrier2 barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = target->swapchain.images[rs->image_idx].handle,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VkDependencyInfo dependency = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier};
    vkCmdPipelineBarrier2(cmd, &dependency);
    VkRenderingAttachmentInfo attachment = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = target->swapchain.images[rs->image_idx].view,
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue.color.float32 = {0.0f, 0.0f, 0.08f, 1.0f}};
    VkRenderingInfo rendering = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, target->extent}, .layerCount = 1,
        .colorAttachmentCount = 1, .pColorAttachments = &attachment};
    vkCmdBeginRendering(cmd, &rendering);
    u32 instances = rect_count + count + header.cursor_visible;
    if (instances) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, trs->pipeline.handle);
        vkCmdPushConstants(cmd, trs->pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(pc), &pc);
        vkCmdDraw(cmd, 4, instances, 0, 0);
    }
    vkCmdEndRendering(cmd);
    barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE; barrier.dstAccessMask = 0;
    barrier.oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier2(cmd, &dependency);
    if (trs->timestamp_pool) vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                                                trs->timestamp_pool, trs->timestamp_base + 1);
    check_vkresult(vkEndCommandBuffer(cmd), SCOPE_GFX_COMMAND_BUFFER, "End text frame");
    enqueue_submit_graphics(rs);
}

// Application waits for device idle before destroying rendering resources.
internal void text_render_cleanup(const GraphicsContext *ctx, TextRenderState *trs) {
    cleanup_pipeline(ctx, &trs->pipeline);
    for (u32 i = 0; i < trs->slot_count; i++) {
        cleanup_buffer(ctx, &trs->slots[i].buffer);
        cleanup_buffer(ctx, &trs->slots[i].rect_buffer);
        cleanup_buffer(ctx, &trs->slots[i].header_buffer);
    }
    cleanup_buffer(ctx, &trs->font_buffer); cleanup_buffer(ctx, &trs->staging);
    *trs = (TextRenderState){0};
}
