/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft;

import org.joml.Matrix4f;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import javax.tools.DiagnosticCollector;
import javax.tools.JavaFileObject;
import javax.tools.ToolProvider;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.stream.Stream;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

class RemixJavaBoundaryTest {
    @Test
    void shimSourcesDoNotImportMinecraftOrFabric(@TempDir Path output) throws Exception {
        try (Stream<Path> sources = Files.walk(Path.of("rtxremix-java", "src"))) {
            List<Path> files = sources.filter(path -> path.toString().endsWith(".java")).toList();
            assertFalse(files.isEmpty());
            assertTrue(compile(files, output));
        }
    }

    @Test
    void isolatedCompilationRejectsGameDependencies(@TempDir Path output) throws Exception {
        for (String type : List.of("net.minecraft.util.Identifier", "net.fabricmc.api.ClientModInitializer")) {
            Path source = output.resolve("ForbiddenDependency.java");
            Files.writeString(source, "class ForbiddenDependency { " + type + " value; }");
            assertFalse(compile(List.of(source), output), type);
        }
    }

    private static boolean compile(List<Path> sources, Path output) throws Exception {
        var compiler = ToolProvider.getSystemJavaCompiler();
        assertNotNull(compiler);
        var diagnostics = new DiagnosticCollector<JavaFileObject>();
        String joml = Path.of(Matrix4f.class.getProtectionDomain().getCodeSource().getLocation().toURI()).toString();
        try (var manager = compiler.getStandardFileManager(diagnostics, null, null)) {
            return compiler.getTask(null, manager, diagnostics,
                    List.of("--release", "21", "-proc:none", "-classpath", joml, "-d", output.toString()),
                    null, manager.getJavaFileObjectsFromPaths(sources)).call();
        }
    }
}
