/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
import com.nvidia.rtxremix.ErrorCode;
import com.nvidia.rtxremix.RemixAPI;
import com.nvidia.rtxremix.RemixBridge;
import com.nvidia.rtxremix.RemixException;
import com.nvidia.rtxremix.PresentInfo;

import java.lang.reflect.Array;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.nio.ByteBuffer;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Set;

/** Native loading/binding test only: the fixtures cannot render or start a game. */
public final class NativeBridgeTest {
    private static final Set<String> WINDOWS_ONLY = Set.of(
            "processCommandLine", "overwriteProcessCommandLine", "dxvkCreateD3D9",
            "dxvkRegisterD3D9Device", "dxvkGetExternalSwapchain", "dxvkGetVkImage",
            "dxvkCopyRenderingOutput", "dxvkSetDefaultOutput", "d3d9CreateDevice",
            "d3d9CreateRenderTarget", "d3d9Reset", "d3d9Present", "d3d9Release");

    public static void main(String[] args) throws Exception {
        Path bridge = Path.of(args[0]).toAbsolutePath();
        System.load(bridge.toString());
        int boundMethods = bindEveryNativeMethod();

        expectError(bridge.resolveSibling("missing-runtime.so"), ErrorCode.LOAD_LIBRARY_FAILURE);
        expectError(Path.of(args[1]), ErrorCode.GET_PROC_ADDRESS_FAILURE);
        expectError(Path.of(args[2]), ErrorCode.INCOMPATIBLE_VERSION);
        expectError(Path.of(args[3]), ErrorCode.INCOMPATIBLE_VERSION);

        Path unicodeCopy = Path.of(args[2]).resolveSibling("тест-🚀-incompatible.so");
        try {
            Files.copy(Path.of(args[2]), unicodeCopy, java.nio.file.StandardCopyOption.REPLACE_EXISTING);
            expectError(unicodeCopy, ErrorCode.INCOMPATIBLE_VERSION);
        } finally {
            Files.deleteIfExists(unicodeCopy);
        }

        try {
            RemixAPI.redactCommandLineOptions("unused", List.of("--accessToken"));
            throw new AssertionError("Linux claimed Windows PEB redaction");
        } catch (UnsupportedOperationException expected) {
            // The Linux bridge has no Windows crash handler or redaction claim.
        }

        int duplicate = RemixBridge.duplicateFileDescriptor(1);
        if (duplicate < 0) throw new AssertionError("No owned duplicate descriptor");
        RemixBridge.closeFileDescriptor(duplicate);
        expectDescriptorFailure(() -> RemixBridge.closeFileDescriptor(duplicate));
        expectDescriptorFailure(() -> RemixBridge.duplicateFileDescriptor(-1));

        RemixAPI api = RemixAPI.load(args[4]);
        if (!api.isInitialized()) throw new AssertionError("Handshake was not recorded");
        api.beginFrame(1280, 720);
        api.beginFrame(0, 0);
        api.injectOverlayInput(3, 65, 1, 2);
        if (RemixBridge.injectOverlayInput(3, 66, 1, 2) != ErrorCode.INVALID_ARGUMENTS) {
            throw new AssertionError("Native overlay input arguments changed in JNI");
        }
        if (RemixBridge.beginFrame(-1, 720) != ErrorCode.INVALID_ARGUMENTS) {
            throw new AssertionError("Negative framebuffer extent reached the runtime");
        }
        PresentInfo present = new PresentInfo();
        present.skipVulkanSwapchain = 1;
        present.interopFlipY = 1;
        present.clientDoneSemaphore_ValueToWait = 42;
        present.disableCpuWaitForExternalSemaphores = 1;
        api.present(present); // Argument forwarding only; the fixture has no frame or swapchain.
        api.shutdown();
        if (api.isInitialized()) throw new AssertionError("Shutdown left facade initialized");
        if (RemixBridge.submit(0) != ErrorCode.NOT_INITIALIZED) {
            throw new AssertionError("Shutdown left the native table active");
        }
        if (RemixBridge.beginFrame(1280, 720) != ErrorCode.NOT_INITIALIZED) {
            throw new AssertionError("Shutdown retained the native frame entry point");
        }
        if (RemixBridge.injectOverlayInput(3, 65, 1, 2) != ErrorCode.NOT_INITIALIZED) {
            throw new AssertionError("Shutdown retained the native overlay input entry point");
        }
        System.out.println("Native ELF JNI bridge: " + boundMethods
                + " methods bound; loader/version/ownership checks passed (no renderer exercised)");
    }

    private static int bindEveryNativeMethod() throws Exception {
        int count = 0;
        for (Method method : RemixBridge.class.getDeclaredMethods()) {
            if (!Modifier.isNative(method.getModifiers())) continue;
            if (method.getName().equals("duplicateFileDescriptor")
                    || method.getName().equals("closeFileDescriptor")) {
                count++;
                continue;
            }
            Object[] parameters = new Object[method.getParameterCount()];
            Class<?>[] types = method.getParameterTypes();
            for (int i = 0; i < parameters.length; i++) parameters[i] = argument(types[i]);
            try {
                Object result = method.invoke(null, parameters);
                if (WINDOWS_ONLY.contains(method.getName())) {
                    throw new AssertionError(method.getName() + " did not reject Windows-only operation");
                }
                if (method.getName().startsWith("alloc")) {
                    long pointer = (Long) result;
                    if (pointer == 0) throw new AssertionError("No extension storage: " + method.getName());
                    RemixBridge.freeExt(pointer);
                }
            } catch (InvocationTargetException failure) {
                if (!WINDOWS_ONLY.contains(method.getName())
                        || !(failure.getCause() instanceof UnsupportedOperationException)) {
                    throw new AssertionError("Native binding failed: " + method.getName(), failure.getCause());
                }
            }
            count++;
        }
        return count;
    }

    private static Object argument(Class<?> type) {
        if (type == int.class) return 0;
        if (type == long.class) return 0L;
        if (type == float.class) return 0F;
        if (type == double.class) return 0D;
        if (type == boolean.class) return false;
        if (type == ByteBuffer.class) return ByteBuffer.allocateDirect(64);
        if (type.isArray()) return Array.newInstance(type.getComponentType(), 16);
        return null;
    }

    private static void expectError(Path runtime, int code) {
        try {
            RemixAPI.load(runtime.toString());
            throw new AssertionError("Accepted invalid runtime " + runtime.getFileName());
        } catch (RemixException expected) {
            if (expected.getErrorCode() != code) {
                throw new AssertionError("Expected runtime error " + code
                        + ", received " + expected.getErrorCode());
            }
        }
    }

    private static void expectDescriptorFailure(Runnable action) {
        try {
            action.run();
            throw new AssertionError("Accepted invalid descriptor ownership");
        } catch (IllegalStateException expected) {
            // Ownership errors must reach the Java caller.
        }
    }
}
