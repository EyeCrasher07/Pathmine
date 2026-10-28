/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 EyeCrasher.
 * SPDX-License-Identifier: Apache-2.0
 */

import com.nvidia.rtxremix.*;
import com.nvidia.rtxremix.minecraft.gl.GlInterop;
import java.awt.image.BufferedImage;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.HexFormat;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Properties;
import java.util.concurrent.TimeUnit;
import javax.imageio.ImageIO;
import org.lwjgl.glfw.GLFWNativeWin32;
import org.lwjgl.opengl.GL;

import static org.lwjgl.glfw.GLFW.*;
import static org.lwjgl.opengl.GL11.*;
import static org.lwjgl.opengl.GL45.glGetTextureImage;

/** Identical API 0.16 inputs for an original Windows SDK and a native Linux SDK. */
public final class RenderParityProbe {
    private static final int WIDTH = 320;
    private static final int HEIGHT = 240;
    private static final int FRAMES = 64;
    private static final boolean LINUX = System.getProperty("os.name").equals("Linux");
    private static final List<String> CASES = List.of(
            "thick_texture_red", "thick_texture_blue", "thin_texture_red", "thin_texture_blue",
            "thick_constant_red", "thick_constant_blue", "thick_diffuse_transmap_red",
            "thick_diffuse_transmap_blue", "thick_diffuse_albedo_only_red",
            "thick_diffuse_albedo_only_blue", "opaque_texture_red", "opaque_texture_blue",
            "diffuse_unlit", "diffuse_white_distant", "diffuse_red_distant", "diffuse_blue_distant",
            "diffuse_white_sphere", "pbr_metal_smooth", "pbr_metal_rough", "pbr_normal_flat",
            "pbr_normal_tilted", "dielectric_ior1000", "dielectric_ior1333", "dielectric_ior1520",
            "alpha_cutout");
    private static final LinkedHashMap<String, String> OPTIONS = new LinkedHashMap<>();
    private static String view = "final";
    private static long nextHash;

    static {
        OPTIONS.put("rtx.graphicsPreset", "4");
        OPTIONS.put("rtx.upscalerType", "0");
        OPTIONS.put("rtx.timeDeltaBetweenFrames", "16");
        OPTIONS.put("rtx.fallbackLightMode", "0");
        OPTIONS.put("rtx.autoExposure.enabled", "False");
        OPTIONS.put("rtx.enableRayReconstruction", "False");
        OPTIONS.put("rtx.initializer.asyncShaderPrewarming", "False");
        OPTIONS.put("rtx.shader.enableAsyncCompilation", "False");
        OPTIONS.put("rtx.translucentMaterial.enableDiffuseLayerOverride", "False");
        OPTIONS.put("rtx.automation.disableBlockingDialogBoxes", "True");
        OPTIONS.put("rtx.showUI", "0");
    }

    private static String digest(Path path) throws Exception {
        var hash = MessageDigest.getInstance("SHA-256");
        try (var input = Files.newInputStream(path)) {
            byte[] bytes = new byte[1024 * 1024];
            for (int count; (count = input.read(bytes)) >= 0;) hash.update(bytes, 0, count);
        }
        return HexFormat.of().formatHex(hash.digest());
    }

    private static Properties verifyBundle(Path root) throws Exception {
        Properties bundle = new Properties();
        try (var input = Files.newInputStream(root.resolve("bundle.xml"))) {
            bundle.loadFromXML(input);
        }
        for (String key : bundle.stringPropertyNames()) {
            if (!key.startsWith("sha256.")) continue;
            Path file = root.resolve(key.substring(7)).normalize();
            if (!file.startsWith(root) || !digest(file).equals(bundle.getProperty(key))) {
                throw new IllegalStateException("Bundle checksum mismatch: " + key);
            }
        }
        for (String required : List.of("probe.jar", "api.jar", "rtx.conf", "views.xml", "observe-options.txt")) {
            if (!bundle.containsKey("sha256." + required)) {
                throw new IllegalStateException("Bundle omits " + required);
            }
        }
        if (!digest(Path.of("rtx.conf")).equals(bundle.getProperty("sha256.rtx.conf"))) {
            throw new IllegalStateException("Run from a fresh work directory with the bundled rtx.conf");
        }
        Path cwd = Path.of("").toAbsolutePath().normalize();
        String configRoot = System.getenv(LINUX ? "XDG_CONFIG_HOME" : "LOCALAPPDATA");
        if (configRoot == null || !Path.of(configRoot).toAbsolutePath().normalize().startsWith(cwd)) {
            throw new IllegalStateException("Use the runner to isolate persistent Remix user settings");
        }
        return bundle;
    }

    private static ByteBuffer buffer(int bytes) {
        return ByteBuffer.allocateDirect(bytes).order(ByteOrder.nativeOrder());
    }

    private static final class Scene implements AutoCloseable {
        private final RemixAPI api;
        private final List<Long> meshes = new ArrayList<>();
        private final List<Long> materials = new ArrayList<>();
        private final List<Long> textures = new ArrayList<>();
        private final List<Long> lights = new ArrayList<>();

        Scene(RemixAPI api) { this.api = api; }

        long material(MaterialInfo.Builder info) {
            long handle = api.createMaterial(info.hash(nextHash++).build());
            materials.add(handle);
            return handle;
        }

        long opaque(float roughness, float metallic, float emission) {
            return material(MaterialInfo.builder().emissiveIntensity(emission).emissiveColorConstant(1, 1, 1)
                    .opaqueExt(MaterialInfoOpaqueEXT.builder().albedoConstant(.6f, .6f, .6f)
                            .opacityConstant(1).roughnessConstant(roughness).metallicConstant(metallic).build()));
        }

        long texture(int size, int purpose, java.util.function.IntFunction<int[]> pixel) {
            ByteBuffer bytes = buffer(size * size * 4);
            for (int index = 0; index < size * size; ++index) {
                for (int channel : pixel.apply(index)) bytes.put((byte) channel);
            }
            bytes.flip();
            long handle = api.createTextureFromMemory(TextureFromMemoryInfo.builder().hash(nextHash++)
                    .pixelData(bytes).size(size, size).format(TextureFormat.RGBA8_UNORM).purpose(purpose).build());
            textures.add(handle);
            return handle;
        }

        void mesh(long material, float[][] positions, float[][] normals, float[][] uvs, int[] indices) {
            var surface = new MeshInfoSurfaceTriangles();
            surface.material = material;
            surface.vertexCount = positions.length;
            surface.indexCount = indices.length;
            surface.vertexBuffer = buffer(positions.length * HardcodedVertex.STRIDE);
            for (int i = 0; i < positions.length; ++i) {
                HardcodedVertex.create(positions[i][0], positions[i][1], positions[i][2],
                        normals[i][0], normals[i][1], normals[i][2], uvs[i][0], uvs[i][1],
                        0xffffffff).writeTo(surface.vertexBuffer);
            }
            surface.vertexBuffer.flip();
            surface.indexBuffer = buffer(indices.length * Integer.BYTES);
            for (int index : indices) surface.indexBuffer.putInt(index);
            surface.indexBuffer.flip();
            meshes.add(api.createMesh(MeshInfo.builder().hash(nextHash++).surfaces(surface).build()));
        }

        void quad(long material, float z, float size, boolean back) {
            float[][] points = {{-size, -size, z}, {-size, size, z}, {size, size, z}, {size, -size, z}};
            float[] normal = {0, 0, back ? 1 : -1};
            mesh(material, points, new float[][] {normal, normal, normal, normal},
                    new float[][] {{0, 0}, {0, 1}, {1, 1}, {1, 0}},
                    back ? new int[] {0, 2, 1, 0, 3, 2} : new int[] {0, 1, 2, 0, 2, 3});
        }

        void sphere(long material) {
            int rings = 16, slices = 32, columns = slices + 1;
            int count = (rings + 1) * columns;
            float[][] positions = new float[count][3], normals = new float[count][3], uvs = new float[count][2];
            for (int ring = 0; ring <= rings; ++ring) {
                double latitude = Math.PI * ring / rings;
                for (int slice = 0; slice <= slices; ++slice) {
                    double longitude = Math.PI * 2 * slice / slices;
                    int index = ring * columns + slice;
                    float x = (float) (StrictMath.sin(latitude) * StrictMath.cos(longitude));
                    float y = (float) StrictMath.cos(latitude);
                    float z = (float) (StrictMath.sin(latitude) * StrictMath.sin(longitude));
                    normals[index] = new float[] {x, y, z};
                    positions[index] = new float[] {x, y, z + 3.5f};
                    uvs[index] = new float[] {(float) slice / slices, (float) ring / rings};
                }
            }
            int[] indices = new int[rings * slices * 6];
            int offset = 0;
            for (int ring = 0; ring < rings; ++ring) for (int slice = 0; slice < slices; ++slice) {
                int a = ring * columns + slice, b = a + 1, c = a + columns;
                for (int index : new int[] {a, b, c, b, c + 1, c}) indices[offset++] = index;
            }
            mesh(material, positions, normals, uvs, indices);
        }

        void sun(float r, float g, float b) {
            lights.add(api.createLight(LightInfo.builder().hash(nextHash++).radiance(r, g, b)
                    .distantExt(LightInfoDistantEXT.builder().direction(.3f, -.4f, .8660254f)
                            .angularDiameterDegrees(1).volumetricRadianceScale(0).build()).build()));
        }

        void reflectionRoom() {
            long floor = opaque(.8f, 0, 0);
            float[] up = {0, 1, 0};
            mesh(floor, new float[][] {{-4, -1.1f, 0}, {-4, -1.1f, 7}, {4, -1.1f, 7}, {4, -1.1f, 0}},
                    new float[][] {up, up, up, up}, new float[][] {{0, 0}, {0, 1}, {1, 1}, {1, 0}},
                    new int[] {0, 1, 2, 0, 2, 3});
            long panel = opaque(1, 0, 3);
            float[] right = {1, 0, 0};
            mesh(panel, new float[][] {{-2, -.5f, 2}, {-2, 2, 2}, {-2, 2, 5}, {-2, -.5f, 5}},
                    new float[][] {right, right, right, right},
                    new float[][] {{0, 0}, {0, 1}, {1, 1}, {1, 0}}, new int[] {0, 1, 2, 0, 2, 3});
            sun(3, 3, 3);
        }

        void draw() {
            for (long mesh : meshes) api.drawInstance(InstanceInfo.builder().mesh(mesh).doubleSided(true).build());
            for (long light : lights) api.drawLightInstance(light);
        }

        @Override public void close() {
            for (long mesh : meshes) api.destroyMesh(mesh);
            for (long light : lights) api.destroyLight(light);
            for (long material : materials) api.destroyMaterial(material);
            for (long texture : textures) api.destroyTexture(texture);
        }
    }

    private static void legacyGlass(Scene scene, int index) {
        int mode = index / 2;
        boolean blue = index % 2 == 1;
        long texture = scene.texture(1, TexturePurpose.COLOR,
                i -> new int[] {blue ? 32 : 224, 32, blue ? 224 : 32, 77});
        var info = MaterialInfo.builder().filterMode(0);
        if (mode == 5) {
            info.albedoTexture(texture).emissiveTexture(texture).emissiveIntensity(.25f)
                    .opaqueExt(MaterialInfoOpaqueEXT.builder().albedoConstant(1, 1, 1).opacityConstant(1)
                            .roughnessConstant(1).metallicConstant(0).build());
        } else {
            var glass = MaterialInfoTranslucentEXT.builder().refractiveIndex(1.52f)
                    .transmittanceColor(.97f, .97f, .97f).transmittanceMeasurementDistance(.35f);
            if (mode == 2) {
                glass.transmittanceColor(blue ? .3f : .85f, .3f, blue ? .85f : .3f);
            } else {
                info.albedoTexture(texture);
                if (mode != 4) glass.transmittanceTexture(texture);
            }
            if (mode == 1) glass.thinWall(1);
            if (mode == 3 || mode == 4) glass.useDiffuseLayer(true);
            info.translucentExt(glass.build());
        }
        long material = scene.material(info);
        scene.quad(material, 3, 2, false);
        scene.quad(material, 3.35f, 2, true);
        long backing = scene.material(MaterialInfo.builder().emissiveIntensity(.25f).emissiveColorConstant(1, 1, 1)
                .opaqueExt(MaterialInfoOpaqueEXT.builder().albedoConstant(1, 1, 1).opacityConstant(1)
                        .roughnessConstant(1).metallicConstant(0).build()));
        scene.quad(backing, 4, 4, false);
    }

    private static void buildScene(Scene scene, String name) {
        int index = CASES.indexOf(name);
        if (index < 12) {
            legacyGlass(scene, index);
        } else if (name.startsWith("diffuse_")) {
            scene.quad(scene.opaque(1, 0, 0), 3, 1.7f, false);
            switch (name) {
                case "diffuse_white_distant" -> scene.sun(4, 4, 4);
                case "diffuse_red_distant" -> scene.sun(4, 0, 0);
                case "diffuse_blue_distant" -> scene.sun(0, 0, 4);
                case "diffuse_white_sphere" -> scene.lights.add(scene.api.createLight(LightInfo.builder()
                        .hash(nextHash++).radiance(100, 100, 100).sphereExt(LightInfoSphereEXT.builder()
                                .position(-1, 1, 1).radius(.15f).volumetricRadianceScale(0).build()).build()));
                case "diffuse_unlit" -> { }
                default -> throw new IllegalArgumentException(name);
            }
        } else if (name.startsWith("pbr_")) {
            long material;
            if (name.startsWith("pbr_metal")) {
                material = scene.opaque(name.endsWith("smooth") ? .05f : .65f, 1, 0);
            } else {
                boolean tilted = name.endsWith("tilted");
                long normal = scene.texture(4, TexturePurpose.NORMAL_DX,
                        i -> new int[] {tilted ? (i % 2 == 0 ? 76 : 180) : 128, 128, tilted ? 235 : 255, 255});
                material = scene.material(MaterialInfo.builder().normalTexture(normal)
                        .opaqueExt(MaterialInfoOpaqueEXT.builder().albedoConstant(.6f, .6f, .6f)
                                .opacityConstant(1).roughnessConstant(.35f).metallicConstant(0).build()));
            }
            scene.sphere(material);
            scene.reflectionRoom();
        } else if (name.startsWith("dielectric_")) {
            float ior = Integer.parseInt(name.substring("dielectric_ior".length())) / 1000f;
            long material = scene.material(MaterialInfo.builder().translucentExt(MaterialInfoTranslucentEXT.builder()
                    .refractiveIndex(ior).transmittanceColor(.97f, .97f, .97f)
                    .transmittanceMeasurementDistance(1).build()));
            scene.sphere(material);
            checkerBacking(scene);
        } else if (name.equals("alpha_cutout")) {
            long checker = scene.texture(8, TexturePurpose.COLOR,
                    i -> new int[] {32, 224, 32, ((i / 8 + i % 8) % 2) * 255});
            long material = scene.material(MaterialInfo.builder().albedoTexture(checker).emissiveTexture(checker)
                    .emissiveIntensity(.25f).opaqueExt(MaterialInfoOpaqueEXT.builder().albedoConstant(1, 1, 1)
                            .opacityConstant(1).roughnessConstant(1).metallicConstant(0)
                            .alphaTestType(4).alphaReferenceValue(127).build()));
            scene.quad(material, 3, 1.7f, false);
            checkerBacking(scene);
        } else {
            throw new IllegalArgumentException(name);
        }
    }

    private static void checkerBacking(Scene scene) {
        long texture = scene.texture(8, TexturePurpose.COLOR,
                i -> (i / 8 + i % 8) % 2 == 0 ? new int[] {224, 80, 32, 255} : new int[] {32, 80, 224, 255});
        long material = scene.material(MaterialInfo.builder().albedoTexture(texture).emissiveTexture(texture)
                .emissiveIntensity(.4f).filterMode(0).opaqueExt(MaterialInfoOpaqueEXT.builder()
                        .albedoConstant(1, 1, 1).opacityConstant(1).roughnessConstant(1).metallicConstant(0).build()));
        scene.quad(material, 5.5f, 4, false);
    }

    private static void options(RemixAPI api) {
        OPTIONS.forEach(api::setConfigVariable);
    }

    private static void configureView(Path bundle, String name) throws Exception {
        var views = new Properties();
        try (var file = Files.newInputStream(bundle.resolve("views.xml"))) {
            views.loadFromXML(file);
        }
        String index = views.getProperty(name + ".index");
        if (index == null) throw new IllegalArgumentException("Unknown diagnostic view: " + name);
        view = name;
        if (name.equals("final")) return;
        OPTIONS.put("rtx.debugView.debugViewIdx", index);
        OPTIONS.put("rtx.debugView.replaceCompositeOutput", "False");
        OPTIONS.put("rtx.debugView.overlayOnTopOfRenderOutput", "False");
        OPTIONS.put("rtx.debugView.composite.compositeViewIdx", "0");
        OPTIONS.put("rtx.debugView.displayType", "0");
        OPTIONS.put("rtx.debugView.enableGammaCorrection", "False");
        OPTIONS.put("rtx.debugView.minValue", views.getProperty(name + ".min", "0"));
        OPTIONS.put("rtx.debugView.maxValue", views.getProperty(name + ".max"));
        OPTIONS.put("rtx.debugView.showFirstGBufferHit", views.getProperty(name + ".firstHit"));
        OPTIONS.put("rtx.debugView.accumulation.enable", "False");
    }

    private static void observeOptions(RemixAPI api, Path bundle, Properties metadata, String phase) throws Exception {
        for (String key : Files.readAllLines(bundle.resolve("observe-options.txt"))) {
            if (key.isBlank() || key.startsWith("#")) continue;
            try {
                metadata.setProperty("observed." + phase + "." + key,
                        api.getConfigVariableResolved(key).toLowerCase(Locale.ROOT));
            } catch (RemixException error) {
                // The API documents GENERAL_FAILURE for an unregistered option name.
                if (error.getErrorCode() != ErrorCode.GENERAL_FAILURE) throw error;
                metadata.setProperty("observed." + phase + "." + key, "unregistered");
            }
        }
    }

    private static void checkOptions(RemixAPI api, Properties metadata) {
        OPTIONS.forEach((key, value) -> {
            String resolved = api.getConfigVariableResolved(key);
            if (!resolved.equalsIgnoreCase(value)) {
                throw new IllegalStateException(key + ": requested " + value + ", resolved " + resolved);
            }
            metadata.setProperty("option." + key, resolved.toLowerCase(Locale.ROOT));
        });
    }

    private static void capture(RemixAPI api, GlInterop interop, Scene scene, Path output) throws Exception {
        var camera = CameraInfo.builder().parameterizedExt(CameraInfoParameterizedEXT.builder()
                .fovYInDegrees(60).aspect((float) WIDTH / HEIGHT).nearPlane(.05f).farPlane(100).build()).build();
        ByteBuffer pixels = buffer(WIDTH * HEIGHT * 4);
        for (int frame = 0; frame < FRAMES; ++frame) {
            if (LINUX) api.beginFrame(WIDTH, HEIGHT);
            api.setupCamera(camera);
            scene.draw();
            api.submit(new SubmitInfo());
            var info = api.dxvkGetInteropInfo();
            if (info.width != WIDTH || info.height != HEIGHT) {
                throw new IllegalStateException("Unexpected extent " + info.width + "x" + info.height);
            }
            interop.recreateImagesIf(info);
            interop.waitForVk(info);
            if (frame == FRAMES - 1) glGetTextureImage(interop.getTexture(), 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            interop.signalDone();
            var present = new PresentInfo();
            present.skipVulkanSwapchain = 1;
            present.clientDoneSemaphore_ValueToWait = interop.getClientDoneSemaphoreValue();
            api.present(present);
            glfwPollEvents();
        }
        var image = new BufferedImage(WIDTH, HEIGHT, BufferedImage.TYPE_INT_RGB);
        for (int y = 0; y < HEIGHT; ++y) for (int x = 0; x < WIDTH; ++x) {
            int offset = (y * WIDTH + x) * 4;
            image.setRGB(x, y, (pixels.get(offset) & 255) << 16
                    | (pixels.get(offset + 1) & 255) << 8 | (pixels.get(offset + 2) & 255));
        }
        if (!ImageIO.write(image, "png", output.toFile())) throw new IllegalStateException("PNG writer unavailable");
        if (view.equals("final")) checkControl(output.getFileName().toString().replace(".png", ""), image);
    }

    private static void checkControl(String name, BufferedImage image) {
        double[] mean = new double[3];
        for (int y = HEIGHT / 2 - 12; y < HEIGHT / 2 + 12; ++y) {
            for (int x = WIDTH / 2 - 12; x < WIDTH / 2 + 12; ++x) {
                int rgb = image.getRGB(x, y);
                mean[0] += (rgb >> 16 & 255) / 576.0;
                mean[1] += (rgb >> 8 & 255) / 576.0;
                mean[2] += (rgb & 255) / 576.0;
            }
        }
        if (name.startsWith("opaque_texture_") || name.startsWith("thick_texture_")
                || name.equals("diffuse_red_distant") || name.equals("diffuse_blue_distant")) {
            int color = name.contains("blue") ? 2 : 0;
            if (mean[color] <= mean[1] + 20) {
                throw new IllegalStateException("Color control is missing its expected primary: " + name);
            }
        }
        if (name.equals("diffuse_unlit") && mean[0] + mean[1] + mean[2] > 1) {
            throw new IllegalStateException("Unlit control receives unexpected illumination");
        }
        if (name.startsWith("diffuse_white_") && mean[0] + mean[1] + mean[2] < 30) {
            throw new IllegalStateException("Analytical light did not illuminate its control: " + name);
        }
    }

    private static void runSuite(Path runtime, Path output, Path probe, List<String> cases) throws Exception {
        Files.createDirectory(output);
        Properties metadata = new Properties();
        metadata.setProperty("completed", "false");
        metadata.setProperty("cleanup.completed", "false");
        try {
            for (int i = 0; i < cases.size(); ++i) {
                String name = cases.get(i);
                System.out.println("scene " + (i + 1) + "/" + cases.size() + ": " + name);
                Path work = Files.createDirectory(output.resolve(name));
                Files.copy(probe.getParent().resolve("rtx.conf"), work.resolve("rtx.conf"));
                Path java = Path.of(System.getProperty("java.home"), "bin", LINUX ? "java" : "java.exe");
                var builder = new ProcessBuilder(java.toString(), "-cp", System.getProperty("java.class.path"),
                        "RenderParityProbe", "--scene", runtime.toString(), work.resolve("result").toString(),
                        "--view=" + view, name);
                builder.directory(work.toFile());
                builder.environment().put("XDG_CONFIG_HOME", work.resolve("config").toString());
                builder.environment().put("LOCALAPPDATA", work.resolve("localappdata").toString());
                builder.redirectOutput(work.resolve("stdout.log").toFile());
                builder.redirectError(work.resolve("stderr.log").toFile());
                Process child = builder.start();
                if (!child.waitFor(90, TimeUnit.SECONDS)) {
                    child.destroyForcibly().waitFor();
                    throw new IllegalStateException("Scene exceeded 90 seconds: " + name);
                }
                if (child.exitValue() != 0) {
                    throw new IllegalStateException("Scene failed with exit " + child.exitValue() + ": " + name);
                }
                Properties captured = new Properties();
                try (var file = Files.newInputStream(work.resolve("result/metadata.xml"))) {
                    captured.loadFromXML(file);
                }
                if (!captured.getProperty("completed").equals("true")
                        || !captured.getProperty("cleanup.completed").equals("true")) {
                    throw new IllegalStateException("Scene or cleanup incomplete: " + name);
                }
                for (String key : captured.stringPropertyNames()) {
                    if (key.startsWith("case.") || key.startsWith("image.")
                            || key.equals("completed") || key.equals("cleanup.completed")) continue;
                    String value = captured.getProperty(key);
                    if (metadata.containsKey(key) && !metadata.getProperty(key).equals(value)) {
                        throw new IllegalStateException("Scene identity changed: " + key);
                    }
                    metadata.setProperty(key, value);
                }
                Files.copy(work.resolve("result/" + name + ".png"), output.resolve(name + ".png"));
                String imageHash = digest(output.resolve(name + ".png"));
                if (!imageHash.equals(captured.getProperty("image." + name + ".sha256"))) {
                    throw new IllegalStateException("Image changed while collecting scene " + name);
                }
                metadata.setProperty("case." + i, name);
                metadata.setProperty("image." + name + ".sha256", imageHash);
            }
            metadata.setProperty("case.count", Integer.toString(cases.size()));
            metadata.setProperty("case.isolation", "fresh-process");
            metadata.setProperty("completed", "true");
            metadata.setProperty("cleanup.completed", "true");
        } finally {
            try (OutputStream file = Files.newOutputStream(output.resolve("metadata.xml"))) {
                metadata.storeToXML(file, "One fresh native runtime process per scene.");
            }
        }
    }

    public static void main(String[] args) throws Exception {
        boolean child = args.length > 0 && args[0].equals("--scene");
        int first = child ? 1 : 0;
        if (args.length < first + 2) throw new IllegalArgumentException("runtime path, new output directory, optional case names");
        Path runtime = Path.of(args[first]).toAbsolutePath();
        Path output = Path.of(args[first + 1]).toAbsolutePath();
        Path probe = Path.of(RenderParityProbe.class.getProtectionDomain().getCodeSource().getLocation().toURI());
        Properties bundle = verifyBundle(probe.getParent());
        int caseStart = first + 2;
        String requestedView = "final";
        if (args.length > caseStart && args[caseStart].startsWith("--view=")) {
            requestedView = args[caseStart++].substring("--view=".length());
        }
        configureView(probe.getParent(), requestedView);
        List<String> cases = args.length == caseStart ? CASES : List.of(args).subList(caseStart, args.length);
        if (cases.isEmpty() || cases.stream().distinct().count() != cases.size() || !CASES.containsAll(cases)) {
            throw new IllegalArgumentException("Cases must be unique names from: " + CASES);
        }
        if (!view.equals("final") && cases.stream().anyMatch(name -> !name.startsWith("pbr_"))) {
            throw new IllegalArgumentException("Intermediate views require an explicit selection of PBR scenes");
        }
        if (!child) {
            runSuite(runtime, output, probe, cases);
            return;
        }
        if (cases.size() != 1) throw new IllegalArgumentException("A scene process accepts exactly one case");
        Files.createDirectory(output);
        Properties metadata = new Properties();
        metadata.setProperty("suite.version", "3");
        metadata.setProperty("probe.sha256", digest(probe));
        metadata.setProperty("api.sha256", bundle.getProperty("sha256.api.jar"));
        metadata.setProperty("config.sha256", bundle.getProperty("sha256.rtx.conf"));
        metadata.setProperty("runtime.sha256", digest(runtime));
        Path core = runtime.resolveSibling("libremix-core.so");
        if (Files.isRegularFile(core)) metadata.setProperty("runtime.core.sha256", digest(core));
        metadata.setProperty("bridge.sha256", digest(runtime.resolveSibling(System.mapLibraryName("remixbridge"))));
        metadata.setProperty("os", System.getProperty("os.name"));
        metadata.setProperty("java", System.getProperty("java.version"));
        metadata.setProperty("capture.width", Integer.toString(WIDTH));
        metadata.setProperty("capture.height", Integer.toString(HEIGHT));
        metadata.setProperty("capture.frames", Integer.toString(FRAMES));
        metadata.setProperty("capture.view", view);
        metadata.setProperty("views.sha256", digest(probe.getParent().resolve("views.xml")));
        metadata.setProperty("observations.sha256", digest(probe.getParent().resolve("observe-options.txt")));
        metadata.setProperty("case.count", Integer.toString(cases.size()));
        metadata.setProperty("case.isolation", "fresh-process");
        metadata.setProperty("completed", "false");
        metadata.setProperty("cleanup.completed", "false");
        if (!glfwInit()) throw new IllegalStateException("GLFW initialization failed");
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        long window = glfwCreateWindow(WIDTH, HEIGHT, "Pathmine parity probe", 0, 0);
        RemixAPI api = null;
        GlInterop interop = null;
        boolean glReady = false;
        try {
            if (window == 0) throw new IllegalStateException("OpenGL window creation failed");
            glfwMakeContextCurrent(window);
            GL.createCapabilities();
            glReady = true;
            metadata.setProperty("gl.renderer", glGetString(GL_RENDERER));
            metadata.setProperty("gl.version", glGetString(GL_VERSION));
            api = RemixAPI.load(runtime.toString());
            options(api);
            api.startup(StartupInfo.builder().hwnd(LINUX ? 0 : GLFWNativeWin32.glfwGetWin32Window(window))
                    .forceNoVkSwapchain(true).interopImage(true).build());
            observeOptions(api, probe.getParent(), metadata, "startup");
            // Keep explicitly requested options above hardware-derived settings.
            options(api);
            interop = new GlInterop(api.dxvkGetInteropInfo());
            var setupCamera = CameraInfo.builder().parameterizedExt(CameraInfoParameterizedEXT.builder()
                    .fovYInDegrees(60).aspect((float) WIDTH / HEIGHT).nearPlane(.05f).farPlane(100).build()).build();
            for (int setupFrame = 0; setupFrame < 4; ++setupFrame) {
                options(api);
                if (LINUX) api.beginFrame(WIDTH, HEIGHT);
                api.setupCamera(setupCamera);
                api.submit(new SubmitInfo());
                var setupInfo = api.dxvkGetInteropInfo();
                if (setupInfo.width != WIDTH || setupInfo.height != HEIGHT) {
                    throw new IllegalStateException("Unexpected empty setup extent");
                }
                interop.recreateImagesIf(setupInfo);
                interop.waitForVk(setupInfo);
                interop.signalDone();
                var setupPresent = new PresentInfo();
                setupPresent.skipVulkanSwapchain = 1;
                setupPresent.clientDoneSemaphore_ValueToWait = interop.getClientDoneSemaphoreValue();
                api.present(setupPresent);
                glfwPollEvents();
            }
            checkOptions(api, metadata);
            metadata.setProperty("capture.setup.frames", "4");
            metadata.setProperty("capture.setup.scene", "empty-camera-no-mesh-material-light");
            observeOptions(api, probe.getParent(), metadata, "settled");
            for (int i = 0; i < cases.size(); ++i) {
                String name = cases.get(i);
                nextHash = 0x525450000L + CASES.indexOf(name) * 0x1000L;
                System.out.println("capture: " + name);
                try (var scene = new Scene(api)) {
                    buildScene(scene, name);
                    capture(api, interop, scene, output.resolve(name + ".png"));
                }
                checkOptions(api, metadata);
                observeOptions(api, probe.getParent(), metadata, "capture");
                metadata.setProperty("case." + i, name);
                metadata.setProperty("image." + name + ".sha256", digest(output.resolve(name + ".png")));
            }
            metadata.setProperty("completed", "true");
        } finally {
            try {
                if (glReady) glFinish();
                if (interop != null) interop.destroy();
                // The original Windows runtime must outlive its subclassed GLFW window.
                if (!LINUX && window != 0) glfwDestroyWindow(window);
                if (api != null) api.shutdown();
                if (LINUX && window != 0) glfwDestroyWindow(window);
                glfwTerminate();
                metadata.setProperty("cleanup.completed", "true");
            } finally {
                try (OutputStream file = Files.newOutputStream(output.resolve("metadata.xml"))) {
                    metadata.storeToXML(file, "Rendered observations; image equality is evaluated separately.");
                }
            }
        }
    }
}
