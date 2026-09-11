#pragma once

void cleanup_pipeline(const GraphicsContext *ctx, Pipeline *pipeline) {
    if (pipeline->handle) vkDestroyPipeline(ctx->device, pipeline->handle, NULL);
    if (pipeline->layout) vkDestroyPipelineLayout(ctx->device, pipeline->layout, NULL);
    *pipeline = (Pipeline){0};
}
