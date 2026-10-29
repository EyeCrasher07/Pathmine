/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
package com.nvidia.rtxremix.minecraft.mixin;

import java.util.List;
import net.minecraft.client.renderer.feature.FeatureFrameContext;
import net.minecraft.client.renderer.feature.RenderTypeFeatureRenderer;
import net.minecraft.client.renderer.feature.submit.SubmitNode;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Invoker;

@Mixin(RenderTypeFeatureRenderer.class)
public interface RenderTypeFeatureRendererAccess {
    @Invoker("buildGroup")
    void rtxremix$buildGroup(FeatureFrameContext context, List<? extends SubmitNode> submits);
}
