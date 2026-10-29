/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft.entity;

import org.joml.Quaternionf;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assertions.assertSame;

import com.mojang.blaze3d.vertex.PoseStack;

class RecyclingMatrixStackTest {

    @Test
    void pooledModelPartTransformMatchesVanillaOperations() {
        float pivotX = 3.25F;
        float pivotY = -7.5F;
        float pivotZ = 12.75F;
        float pitch = 0.31F;
        float yaw = -0.52F;
        float roll = 0.73F;
        float xScale = 0.8F;
        float yScale = 1.1F;
        float zScale = 1.3F;

        PoseStack vanilla = new PoseStack();
        vanilla.translate(pivotX / 16.0F, pivotY / 16.0F, pivotZ / 16.0F);
        vanilla.rotate(new Quaternionf().rotationZYX(roll, yaw, pitch));
        vanilla.scale(xScale, yScale, zScale);

        RecyclingMatrixStack recycling = new RecyclingMatrixStack();
        recycling.applyModelPartTransform(
                pivotX, pivotY, pivotZ,
                pitch, yaw, roll,
                xScale, yScale, zScale);

        assertTrue(vanilla.last().pose()
                .equals(recycling.last().pose(), 0.0F));
        assertTrue(vanilla.last().normal()
                .equals(recycling.last().normal(), 0.0F));
    }

    @Test
    void identityRotationAndScaleMatchVanillaTranslation() {
        PoseStack vanilla = new PoseStack();
        vanilla.translate(0.5F, -0.25F, 1.5F);

        RecyclingMatrixStack recycling = new RecyclingMatrixStack();
        recycling.applyModelPartTransform(
                8.0F, -4.0F, 24.0F,
                0.0F, 0.0F, 0.0F,
                1.0F, 1.0F, 1.0F);

        assertTrue(vanilla.last().pose()
                .equals(recycling.last().pose(), 0.0F));
        assertTrue(vanilla.last().normal()
                .equals(recycling.last().normal(), 0.0F));
    }

    @Test
    void reusedEntryCopiesTheCurrentPoseAndNormals() {
        RecyclingMatrixStack stack = new RecyclingMatrixStack();
        stack.pushPose();
        PoseStack.Pose child = stack.last();
        stack.scale(2, 3, 4);
        stack.popPose();

        stack.rotate(new Quaternionf().rotationXYZ(0.1f, -0.2f, 0.3f));
        stack.scale(-0.5f, 2f, 3f);
        var expectedPose = new org.joml.Matrix4f(stack.last().pose());
        var expectedNormal = new org.joml.Matrix3f(stack.last().normal());
        stack.pushPose();

        assertSame(child, stack.last());
        assertTrue(expectedPose.equals(stack.last().pose(), 0f));
        assertTrue(expectedNormal.equals(stack.last().normal(), 0f));
    }

    @Test
    void resetAfterAnInterruptedNestedCaptureRestoresTheRootAndRetainsThePool() {
        RecyclingMatrixStack stack = new RecyclingMatrixStack();
        PoseStack.Pose root = stack.last();
        stack.translate(5, 6, 7);
        stack.pushPose();
        PoseStack.Pose firstChild = stack.last();
        stack.rotate(new Quaternionf().rotationY(0.5f));
        stack.pushPose();
        stack.scale(2, 3, 4);

        stack.reset();

        assertTrue(stack.isEmpty());
        assertSame(root, stack.last());
        assertTrue(new org.joml.Matrix4f().equals(stack.last().pose(), 0f));
        assertTrue(new org.joml.Matrix3f().equals(stack.last().normal(), 0f));
        stack.pushPose();
        assertSame(firstChild, stack.last());
        assertTrue(new org.joml.Matrix4f().equals(stack.last().pose(), 0f));
        assertTrue(new org.joml.Matrix3f().equals(stack.last().normal(), 0f));
    }

}
