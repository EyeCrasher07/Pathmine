/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft;

import com.mojang.brigadier.CommandDispatcher;
import com.mojang.brigadier.tree.CommandNode;
import net.fabricmc.fabric.api.client.command.v2.ClientCommandRegistrationCallback;
import net.fabricmc.fabric.api.client.command.v2.FabricClientCommandSource;
import org.junit.jupiter.api.Test;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;

class InGameGuideCommandTest {
    private static final Path GUIDE = Path.of("docs", "IN_GAME_GUIDE.md");
    private static final Pattern DOCUMENTED_COMMAND = Pattern.compile(
            "/rtx(?:\\s+([A-Za-z][A-Za-z0-9_-]*))?(?:\\s+([A-Za-z][A-Za-z0-9_-]*))?");

    @Test
    void everyDocumentedCommandIsRegistered() throws Exception {
        var register = RtxRemixMod.class.getDeclaredMethod("registerCommands");
        register.setAccessible(true);
        register.invoke(new RtxRemixMod());
        CommandDispatcher<FabricClientCommandSource> dispatcher = new CommandDispatcher<>();
        ClientCommandRegistrationCallback.EVENT.invoker().register(dispatcher, null);

        List<String> documented = documentedCommandPaths();
        assertFalse(documented.isEmpty(), GUIDE + " has no command rows");
        for (String path : documented) {
            CommandNode<FabricClientCommandSource> node = dispatcher.getRoot().getChild("rtx");
            assertNotNull(node, "/rtx is not registered");
            for (String literal : path.split(" ")) {
                node = node.getChild(literal);
                assertNotNull(node, "/rtx " + path + " is not registered");
            }
        }
    }

    private static List<String> documentedCommandPaths() throws Exception {
        List<String> paths = new ArrayList<>();
        for (String line : Files.readAllLines(GUIDE)) {
            if (!line.stripLeading().startsWith("|")) {
                continue;
            }
            Matcher matcher = DOCUMENTED_COMMAND.matcher(line);
            while (matcher.find()) {
                List<String> literals = new ArrayList<>();
                for (int group = 1; group <= matcher.groupCount() && matcher.group(group) != null; group++) {
                    literals.add(matcher.group(group));
                }
                if (!literals.isEmpty()) {
                    paths.add(String.join(" ", literals));
                }
            }
        }
        return paths;
    }
}
