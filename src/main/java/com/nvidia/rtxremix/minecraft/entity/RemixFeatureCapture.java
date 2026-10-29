/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
package com.nvidia.rtxremix.minecraft.entity;

import com.nvidia.rtxremix.minecraft.mixin.RenderTypeFeatureRendererAccess;
import java.util.AbstractList;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.NoSuchElementException;
import net.fabricmc.fabric.impl.client.rendering.FeatureRendererRegistryImpl;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.SubmitNodeStorage;
import net.minecraft.client.renderer.feature.*;
import net.minecraft.client.renderer.feature.submit.SubmitNode;

/** Runs Minecraft's feature tessellators against the current owner's Remix consumers. */
public final class RemixFeatureCapture {
    private static final ThreadLocal<RemixFeatureCapture> CURRENT = new ThreadLocal<>();

    private final RemixVertexConsumerProvider provider;
    private final SubmitNodeStorage submits = new SubmitNodeStorage();
    private final List<Level> levels = new ArrayList<>();
    private RemixFeatureCapture previous;
    private FeatureFrameContext context;
    private int depth;
    private boolean open;

    RemixFeatureCapture(RemixVertexConsumerProvider provider) {
        this.provider = provider;
    }

    public static RemixFeatureCapture current() {
        return CURRENT.get();
    }

    public RemixVertexConsumerProvider provider() {
        return provider;
    }

    public SubmitNodeStorage submits() {
        return submits;
    }

    void open() {
        if (!open) {
            previous = CURRENT.get();
            CURRENT.set(this);
            open = true;
        }
    }

    void close() {
        if (open) {
            if (CURRENT.get() != this) {
                throw new IllegalStateException("Feature captures closed out of order");
            }
            CURRENT.set(previous);
            previous = null;
            open = false;
        }
    }

    public void capture(SubmitNode submit) {
        // The retained scene supplies traced shadows rather than Minecraft's ground blobs.
        if (submit.featureType() == ShadowFeatureRenderer.TYPE) {
            return;
        }
        if (depth == levels.size()) {
            levels.add(new Level());
        }
        Level level = levels.get(depth++);
        level.submit.value = submit;
        try {
            var renderer = level.renderers.getOrThrow(submit.featureType());
            if (!(renderer instanceof RenderTypeFeatureRenderer<?>)) {
                throw new IllegalArgumentException("Feature has no vertex capture path: " + submit.featureType());
            }
            ((RenderTypeFeatureRendererAccess) renderer).rtxremix$buildGroup(context(), level.submit);
        } finally {
            level.submit.value = null;
            depth--;
        }
    }

    private FeatureFrameContext context() {
        Minecraft client = Minecraft.getInstance();
        var models = client.getModelManager().getBlockStateModelSet();
        var lightmap = client.gameRenderer.lightmap();
        if (context == null || context.blockStateModelSet() != models || context.lightmap() != lightmap) {
            context = new FeatureFrameContext(client.gameRenderer.gameRenderState().optionsRenderState,
                    client.font, models, client.getBlockColors(), client.getTextureManager(),
                    client.getAtlasManager(), lightmap, client.gameRenderer.renderBuffers().stagedVertexBuffer());
        }
        return context;
    }

    static FeatureRendererMap createRenderers() {
        FeatureRendererMap renderers = new FeatureRendererMap();
        renderers.put(ModelFeatureRenderer.TYPE, new ModelFeatureRenderer());
        renderers.put(ItemFeatureRenderer.TYPE, new ItemFeatureRenderer());
        renderers.put(TextFeatureRenderer.TYPE, new TextFeatureRenderer());
        renderers.put(CustomFeatureRenderer.TYPE, new CustomFeatureRenderer());
        renderers.put(FlameFeatureRenderer.TYPE, new FlameFeatureRenderer());
        renderers.put(LeashFeatureRenderer.TYPE, new LeashFeatureRenderer());
        renderers.put(BlockModelFeatureRenderer.TYPE, new BlockModelFeatureRenderer());
        renderers.put(MovingBlockFeatureRenderer.TYPE, new MovingBlockFeatureRenderer());
        renderers.put(ShapeOutlineFeatureRenderer.TYPE, new ShapeOutlineFeatureRenderer());
        renderers.put(GizmoFeatureRenderer.TYPE, new GizmoFeatureRenderer());
        // Fabric exposes registration publicly, but its dispatcher factory is internal.
        // Use that factory so the active renderer and other mods supply their own tessellators.
        FeatureRendererRegistryImpl.registerRenderers(renderers);
        return renderers;
    }

    private static final class Level {
        final FeatureRendererMap renderers = createRenderers();
        final SingleSubmit submit = new SingleSubmit();
    }

    /** One list and iterator per nesting level; nested model renders never overwrite a borrower. */
    private static final class SingleSubmit extends AbstractList<SubmitNode> {
        SubmitNode value;
        private boolean consumed;
        private final Iterator<SubmitNode> iterator = new Iterator<>() {
            @Override public boolean hasNext() { return !consumed; }
            @Override public SubmitNode next() {
                if (consumed) throw new NoSuchElementException();
                consumed = true;
                return value;
            }
        };

        @Override public int size() { return 1; }
        @Override public SubmitNode get(int index) {
            if (index != 0) throw new IndexOutOfBoundsException(index);
            return value;
        }
        @Override public Iterator<SubmitNode> iterator() {
            consumed = false;
            return iterator;
        }
    }
}
