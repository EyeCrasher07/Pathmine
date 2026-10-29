/* SPDX-License-Identifier: GPL-3.0-only */
package com.nvidia.rtxremix.minecraft.mixin;

import com.google.gson.JsonParser;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;
import org.objectweb.asm.ClassReader;
import org.objectweb.asm.Type;
import org.objectweb.asm.tree.AnnotationNode;
import org.objectweb.asm.tree.ClassNode;
import org.objectweb.asm.tree.MethodNode;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;

/** Checks injection arguments against the resolved game classes without initializing them. */
class MixinInjectionContractTest {
    private final Map<String, ClassNode> classes = new HashMap<>();

    @Test
    void callbacksMatchTheActualTargetDescriptors() throws Exception {
        List<String> mismatches = new ArrayList<>();
        try (var input = getClass().getClassLoader().getResourceAsStream("rtx-remix-renderer.mixins.json")) {
            assertNotNull(input);
            var config = JsonParser.parseReader(new InputStreamReader(input, StandardCharsets.UTF_8)).getAsJsonObject();
            String prefix = config.get("package").getAsString().replace('.', '/') + "/";
            for (String side : List.of("mixins", "client", "server")) {
                var entries = config.getAsJsonArray(side);
                if (entries == null) continue;
                for (var entry : entries) inspect(read(prefix + entry.getAsString()), mismatches);
            }
        }
        assertEquals(List.of(), mismatches, "injection callbacks must follow the pinned Minecraft API");
    }

    private void inspect(ClassNode mixin, List<String> mismatches) throws IOException {
        AnnotationNode annotation = find(mixin.invisibleAnnotations, "Lorg/spongepowered/asm/mixin/Mixin;");
        assertNotNull(annotation, mixin.name);
        List<String> targets = new ArrayList<>();
        if (value(annotation, "value") instanceof List<?> values) {
            for (Object target : values) targets.add(((Type) target).getInternalName());
        }
        if (value(annotation, "targets") instanceof List<?> values) {
            for (Object target : values) targets.add(target.toString().replace('.', '/'));
        }
        for (String target : targets) {
            ClassNode targetClass = read(target);
            for (MethodNode handler : mixin.methods) {
                AnnotationNode injection = find(handler.visibleAnnotations, "Lorg/spongepowered/asm/mixin/injection/Inject;");
                if (injection == null) continue;
                for (Object selector : (List<?>) value(injection, "method")) {
                    String name = selector.toString();
                    int descriptor = name.indexOf('(');
                    String methodName = descriptor < 0 ? name : name.substring(0, descriptor);
                    List<MethodNode> matches = targetClass.methods.stream()
                            .filter(method -> method.name.equals(methodName)
                                    && (descriptor < 0 || method.desc.equals(name.substring(descriptor))))
                            .toList();
                    if (matches.isEmpty()) mismatches.add(mixin.name + ": missing " + target + "." + name);
                    for (MethodNode method : matches) checkArguments(mixin.name, handler, method, mismatches);
                }
            }
        }
    }

    private static void checkArguments(String mixin, MethodNode handler, MethodNode target, List<String> mismatches) {
        Type[] args = Type.getArgumentTypes(handler.desc);
        Type[] expected = Type.getArgumentTypes(target.desc);
        for (int i = 0; i < args.length; i++) {
            String type = args[i].getDescriptor();
            if (!type.startsWith("Lorg/spongepowered/asm/mixin/injection/callback/CallbackInfo")) continue;
            // A callback alone deliberately captures none of the target arguments.
            if (i == 0) return;
            if (i != expected.length || !List.of(args).subList(0, i).equals(List.of(expected))) {
                mismatches.add(mixin + "." + handler.name + handler.desc + " targets " + target.name + target.desc);
            }
            return;
        }
    }

    private ClassNode read(String name) throws IOException {
        ClassNode cached = classes.get(name);
        if (cached != null) return cached;
        try (var input = getClass().getClassLoader().getResourceAsStream(name + ".class")) {
            assertNotNull(input, name);
            ClassNode node = new ClassNode();
            new ClassReader(input).accept(node, ClassReader.SKIP_CODE | ClassReader.SKIP_DEBUG | ClassReader.SKIP_FRAMES);
            classes.put(name, node);
            return node;
        }
    }

    private static AnnotationNode find(List<AnnotationNode> annotations, String descriptor) {
        if (annotations == null) return null;
        return annotations.stream().filter(annotation -> annotation.desc.equals(descriptor)).findFirst().orElse(null);
    }

    private static Object value(AnnotationNode annotation, String key) {
        if (annotation.values == null) return null;
        for (int i = 0; i < annotation.values.size(); i += 2) {
            if (key.equals(annotation.values.get(i))) return annotation.values.get(i + 1);
        }
        return null;
    }
}
