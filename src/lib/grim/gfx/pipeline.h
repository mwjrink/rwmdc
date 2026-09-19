#pragma once

#include <lib/grim/gfx/internal_graphics.h>

VkFormat format_pick(rop(ro GraphicsContext) ctx,
                     AllocBuff            candidates,
                     VkImageTiling        tiling,
                     VkFormatFeatureFlags features) {

    for (u32 idx = 0; idx < candidates.len; idx++) {
        VkFormatProperties props;
        VkFormat           candidate = ((VkFormat*)candidates.data)[idx];
        vkGetPhysicalDeviceFormatProperties(ctx->physical_device.handle, candidate, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return candidate;
        } else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return candidate;
        }
    }

    CRITICAL_LOG(SCOPE_GFX_PIPELINE, "Failed to find a suitable format.");
    exit(1);
}

u32 format_is_depth(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
           format == VK_FORMAT_D24_UNORM_S8_UINT;
}

VkFormat format_pick_depth(rop(ro GraphicsContext) ctx) {
    VkFormat  formats[]  = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT};
    AllocBuff candidates = {
        .data = &formats,
        .len  = sizeof(formats) / sizeof(VkFormat),
    };

    return format_pick(ctx, candidates, VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

u32 format_has_stencil_component(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

Pipeline create_glyph_pipeline(rop(rw Arena) arena, rop(ro GraphicsContext) ctx, rop(ro RenderTarget) render_target) {
    VkShaderModule* shaders = arena_alloc_align(arena, sizeof(VkShaderModule), sizeof(VkShaderModule) * 2);
    arena_ckpt(arena);
    u32 shader_count = 0;

    VkPipelineShaderStageCreateInfo shader_create_infos[2] = {0};
    {
        shaders[shader_count]                    = read_shader(arena, ctx, "assets/shaders/text.vert.spv");
        shader_create_infos[shader_count].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shader_create_infos[shader_count].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        shader_create_infos[shader_count].module = shaders[shader_count];
        shader_create_infos[shader_count].pName  = "main";
        shader_count += 1;
    }

    {
        shaders[shader_count]                    = read_shader(arena, ctx, "assets/shaders/text.frag.spv");
        shader_create_infos[shader_count].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shader_create_infos[shader_count].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        shader_create_infos[shader_count].module = shaders[shader_count];
        shader_create_infos[shader_count].pName  = "main";
        shader_count += 1;
    }

    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamic_state = {0};
    dynamic_state.sType                            = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount                = sizeof(dynamic_states) / sizeof(VkDynamicState);
    dynamic_state.pDynamicStates                   = dynamic_states;

    VkPipelineVertexInputStateCreateInfo vertex_input_info = {0};
    vertex_input_info.sType                                = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount        = 0;
    vertex_input_info.pVertexBindingDescriptions           = NULL;
    vertex_input_info.vertexAttributeDescriptionCount      = 0;
    vertex_input_info.pVertexAttributeDescriptions         = NULL;

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {0};
    input_assembly.sType                                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology                               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    input_assembly.primitiveRestartEnable                 = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewport_state = {0};
    viewport_state.sType                             = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount                     = 1;
    viewport_state.scissorCount                      = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer = {0};
    rasterizer.sType                                  = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable                       = VK_FALSE;
    rasterizer.rasterizerDiscardEnable                = VK_FALSE;
    rasterizer.polygonMode                            = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth                              = 1.0f;
    rasterizer.cullMode                               = VK_CULL_MODE_NONE;
    rasterizer.frontFace                              = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable                        = VK_FALSE;
    rasterizer.depthBiasConstantFactor                = 0.0f;
    rasterizer.depthBiasClamp                         = 0.0f;
    rasterizer.depthBiasSlopeFactor                   = 0.0f;

    VkPipelineMultisampleStateCreateInfo multisampling = {0};
    multisampling.sType                                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable                  = VK_FALSE;
    multisampling.rasterizationSamples                 = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading                     = 1.0f;
    multisampling.pSampleMask                          = NULL;
    multisampling.alphaToCoverageEnable                = VK_FALSE;
    multisampling.alphaToOneEnable                     = VK_FALSE;

    VkPipelineColorBlendAttachmentState color_blend_attachment = {0};
    color_blend_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable         = VK_TRUE;
    color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment.colorBlendOp        = VK_BLEND_OP_ADD;
    color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment.alphaBlendOp        = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo color_blending = {0};
    color_blending.sType                               = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable                       = VK_FALSE;
    color_blending.logicOp                             = VK_LOGIC_OP_COPY;
    color_blending.attachmentCount                     = 1;
    color_blending.pAttachments                        = &color_blend_attachment;
    color_blending.blendConstants[0]                   = 0.0f;
    color_blending.blendConstants[1]                   = 0.0f;
    color_blending.blendConstants[2]                   = 0.0f;
    color_blending.blendConstants[3]                   = 0.0f;

    VkPushConstantRange vert_range = {0};
    vert_range.stageFlags          = VK_SHADER_STAGE_VERTEX_BIT;
    vert_range.offset              = 0;
    vert_range.size                = sizeof(PushConstant_TextVert);

    VkPushConstantRange frag_range = {0};
    frag_range.stageFlags          = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_range.offset              = sizeof(PushConstant_TextVert);
    frag_range.size                = sizeof(PushConstant_TextFrag);

    VkPushConstantRange push_constants[] = {vert_range, frag_range};

    VkPipelineLayoutCreateInfo layout_create_info = {0};
    layout_create_info.sType                      = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_create_info.pushConstantRangeCount     = sizeof(push_constants) / sizeof(VkPushConstantRange);
    layout_create_info.pPushConstantRanges        = push_constants;

    VkPipelineLayout layout = {0};
    {
        VkResult result = vkCreatePipelineLayout(ctx->device, &layout_create_info, NULL, &layout);
        check_vkresult(result, SCOPE_GFX_PIPELINE, "Failed to create pipeline layout.");
    }

    VkPipelineRenderingCreateInfo rendering_create_info = {0};
    rendering_create_info.sType                         = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering_create_info.colorAttachmentCount          = 1;
    rendering_create_info.pColorAttachmentFormats       = &render_target->format;

    VkGraphicsPipelineCreateInfo create_info = {0};
    create_info.sType                        = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    create_info.stageCount                   = shader_count;
    create_info.pStages                      = shader_create_infos;
    create_info.pVertexInputState            = &vertex_input_info;
    create_info.pInputAssemblyState          = &input_assembly;
    create_info.pViewportState               = &viewport_state;
    create_info.pRasterizationState          = &rasterizer;
    create_info.pMultisampleState            = &multisampling;
    create_info.pColorBlendState             = &color_blending;
    create_info.renderPass                   = VK_NULL_HANDLE;
    create_info.pDynamicState                = &dynamic_state;
    create_info.layout                       = layout;
    create_info.basePipelineHandle           = NULL;
    create_info.basePipelineIndex            = -1;
    create_info.pNext                        = &rendering_create_info;

    VkPipeline pipeline = {0};

    // TODO use a pipeline cache? or library, whichever is newer
    VkResult result = vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 1, &create_info, NULL, &pipeline);
    check_vkresult(result, SCOPE_GFX_PIPELINE, "Failed to create graphics pipeline.");

    arena_pop(arena);

    return (Pipeline){
        .handle      = pipeline,
        .stage_count = shader_count,
        .shaders     = shaders,
        .layout      = layout,
    };
}

Pipeline create_rect_pipeline(rop(rw Arena) arena, rop(ro GraphicsContext) ctx, rop(ro RenderTarget) render_target) {
    VkShaderModule* shaders = arena_alloc_align(arena, sizeof(VkShaderModule), sizeof(VkShaderModule) * 2);
    arena_ckpt(arena);
    u32 shader_count = 0;

    VkPipelineShaderStageCreateInfo shader_create_infos[2] = {0};
    {
        shaders[shader_count]                    = read_shader(arena, ctx, "assets/shaders/rect.vert.spv");
        shader_create_infos[shader_count].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shader_create_infos[shader_count].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        shader_create_infos[shader_count].module = shaders[shader_count];
        shader_create_infos[shader_count].pName  = "main";
        shader_count += 1;
    }

    {
        shaders[shader_count]                    = read_shader(arena, ctx, "assets/shaders/rect.frag.spv");
        shader_create_infos[shader_count].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shader_create_infos[shader_count].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        shader_create_infos[shader_count].module = shaders[shader_count];
        shader_create_infos[shader_count].pName  = "main";
        shader_count += 1;
    }

    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamic_state = {0};
    dynamic_state.sType                            = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount                = sizeof(dynamic_states) / sizeof(VkDynamicState);
    dynamic_state.pDynamicStates                   = dynamic_states;

    VkPipelineVertexInputStateCreateInfo vertex_input_info = {0};
    vertex_input_info.sType                                = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount        = 0;
    vertex_input_info.pVertexBindingDescriptions           = NULL;
    vertex_input_info.vertexAttributeDescriptionCount      = 0;
    vertex_input_info.pVertexAttributeDescriptions         = NULL;

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {0};
    input_assembly.sType                                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology                               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    input_assembly.primitiveRestartEnable                 = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewport_state = {0};
    viewport_state.sType                             = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount                     = 1;
    viewport_state.scissorCount                      = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer = {0};
    rasterizer.sType                                  = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable                       = VK_FALSE;
    rasterizer.rasterizerDiscardEnable                = VK_FALSE;
    rasterizer.polygonMode                            = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth                              = 1.0f;
    rasterizer.cullMode                               = VK_CULL_MODE_NONE;
    rasterizer.frontFace                              = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable                        = VK_FALSE;
    rasterizer.depthBiasConstantFactor                = 0.0f;
    rasterizer.depthBiasClamp                         = 0.0f;
    rasterizer.depthBiasSlopeFactor                   = 0.0f;

    VkPipelineMultisampleStateCreateInfo multisampling = {0};
    multisampling.sType                                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable                  = VK_FALSE;
    multisampling.rasterizationSamples                 = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading                     = 1.0f;
    multisampling.pSampleMask                          = NULL;
    multisampling.alphaToCoverageEnable                = VK_FALSE;
    multisampling.alphaToOneEnable                     = VK_FALSE;

    VkPipelineColorBlendAttachmentState color_blend_attachment = {0};
    color_blend_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable         = VK_TRUE;
    color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment.colorBlendOp        = VK_BLEND_OP_ADD;
    color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment.alphaBlendOp        = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo color_blending = {0};
    color_blending.sType                               = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable                       = VK_FALSE;
    color_blending.logicOp                             = VK_LOGIC_OP_COPY;
    color_blending.attachmentCount                     = 1;
    color_blending.pAttachments                        = &color_blend_attachment;
    color_blending.blendConstants[0]                   = 0.0f;
    color_blending.blendConstants[1]                   = 0.0f;
    color_blending.blendConstants[2]                   = 0.0f;
    color_blending.blendConstants[3]                   = 0.0f;

    VkPushConstantRange push_constant = {0};
    push_constant.stageFlags          = VK_SHADER_STAGE_VERTEX_BIT;
    push_constant.offset              = 0;
    push_constant.size                = sizeof(PushConstant_Rect);

    VkPushConstantRange push_constants[] = {push_constant};

    VkPipelineLayoutCreateInfo layout_create_info = {0};
    layout_create_info.sType                      = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_create_info.pushConstantRangeCount     = sizeof(push_constants) / sizeof(VkPushConstantRange);
    layout_create_info.pPushConstantRanges        = push_constants;

    VkPipelineLayout layout = {0};
    {
        VkResult result = vkCreatePipelineLayout(ctx->device, &layout_create_info, NULL, &layout);
        check_vkresult(result, SCOPE_GFX_PIPELINE, "Failed to create pipeline layout.");
    }

    VkPipelineRenderingCreateInfo rendering_create_info = {0};
    rendering_create_info.sType                         = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering_create_info.colorAttachmentCount          = 1;
    rendering_create_info.pColorAttachmentFormats       = &render_target->format;

    VkGraphicsPipelineCreateInfo create_info = {0};
    create_info.sType                        = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    create_info.stageCount                   = shader_count;
    create_info.pStages                      = shader_create_infos;
    create_info.pVertexInputState            = &vertex_input_info;
    create_info.pInputAssemblyState          = &input_assembly;
    create_info.pViewportState               = &viewport_state;
    create_info.pRasterizationState          = &rasterizer;
    create_info.pMultisampleState            = &multisampling;
    create_info.pColorBlendState             = &color_blending;
    create_info.renderPass                   = VK_NULL_HANDLE;
    create_info.pDynamicState                = &dynamic_state;
    create_info.layout                       = layout;
    create_info.basePipelineHandle           = NULL;
    create_info.basePipelineIndex            = -1;
    create_info.pNext                        = &rendering_create_info;

    VkPipeline pipeline = {0};

    // TODO use a pipeline cache? or library, whichever is newer
    VkResult result = vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 1, &create_info, NULL, &pipeline);
    check_vkresult(result, SCOPE_GFX_PIPELINE, "Failed to create graphics pipeline.");

    arena_pop(arena);

    return (Pipeline){
        .handle        = pipeline,
        .stage_count   = shader_count,
        .shaders       = shaders,
        .layout        = layout,
        .pipeline_type = PipelineType_Graphics,
    };
}

void cleanup_pipeline(rop(ro GraphicsContext) ctx, rop(rw Pipeline) pipeline) {
    for (u32 idx = 0; idx < pipeline->stage_count; idx++) {
        cleanup_shader(ctx, &(pipeline->shaders[idx]));
        pipeline->shaders[idx] = VK_NULL_HANDLE;
    }
    pipeline->stage_count = 0;
    pipeline->shaders     = NULL;

    vkDestroyPipeline(ctx->device, pipeline->handle, NULL);
    pipeline->handle = VK_NULL_HANDLE;

    vkDestroyPipelineLayout(ctx->device, pipeline->layout, NULL);
    pipeline->layout = VK_NULL_HANDLE;
}
