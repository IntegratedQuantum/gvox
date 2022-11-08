#include <gvox/gvox.h>

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <memory.h>
#include <algorithm>
#include <unordered_set>
#include <array>

#include <iostream>

#if __linux__
#define EXPORT
#elif _WIN32
#define EXPORT __declspec(dllexport)
#endif

static constexpr auto CHUNK_SIZE = 8;

uint32_t ceil_log2(size_t x) {
    static const size_t t[6] = {
        0xFFFFFFFF00000000ull,
        0x00000000FFFF0000ull,
        0x000000000000FF00ull,
        0x00000000000000F0ull,
        0x000000000000000Cull,
        0x0000000000000002ull};

    uint32_t y = (((x & (x - 1)) == 0) ? 0 : 1);
    int j = 32;
    int i;

    for (i = 0; i < 6; i++) {
        int k = (((x & t[i]) == 0) ? 0 : j);
        y += k;
        x >>= k;
        j >>= 1;
    }

    return y;
}

template <typename T>
static void write_data(uint8_t *&buffer_ptr, T const &data) {
    *reinterpret_cast<T *>(buffer_ptr) = data;
    buffer_ptr += sizeof(T);
}

template <typename T>
static T read_data(uint8_t *&buffer_ptr) {
    auto &result = *reinterpret_cast<T *>(buffer_ptr);
    buffer_ptr += sizeof(T);
    return result;
}

static constexpr std::array<uint32_t, 9> mask_bases = {
    0b0000'0000'0000'0001,
    0b0000'0000'0000'0011,
    0b0000'0000'0000'0111,
    0b0000'0000'0000'1111,
    0b0000'0000'0001'1111,
    0b0000'0000'0011'1111,
    0b0000'0000'0111'1111,
    0b0000'0000'1111'1111,
    0b0000'0001'1111'1111,
};

struct PaletteCompressor {
    std::vector<uint8_t> data;

    size_t chunk_variance(GVoxSceneNode const &node, size_t chunk_x, size_t chunk_y, size_t chunk_z) {
        auto tile_set = std::unordered_set<uint32_t>{};
        size_t ox = chunk_x * CHUNK_SIZE;
        size_t oy = chunk_y * CHUNK_SIZE;
        size_t oz = chunk_z * CHUNK_SIZE;
        // std::cout << "o: " << ox << " " << oy << " " << oz << "\t";

        for (size_t zi = 0; zi < CHUNK_SIZE; ++zi) {
            for (size_t yi = 0; yi < CHUNK_SIZE; ++yi) {
                for (size_t xi = 0; xi < CHUNK_SIZE; ++xi) {
                    // TODO: fix for non-multiple sizes of CHUNK_SIZE
                    size_t px = ox + xi;
                    size_t py = oy + yi;
                    size_t pz = oz + zi;
                    size_t index = px + py * node.size_x + pz * node.size_x * node.size_y;
                    auto const &i_vox = node.voxels[index];

                    uint32_t u32_voxel = 0;
                    uint32_t r = static_cast<uint32_t>(std::max(std::min(i_vox.color.x, 1.0f), 0.0f) * 255.0f);
                    uint32_t g = static_cast<uint32_t>(std::max(std::min(i_vox.color.y, 1.0f), 0.0f) * 255.0f);
                    uint32_t b = static_cast<uint32_t>(std::max(std::min(i_vox.color.z, 1.0f), 0.0f) * 255.0f);
                    uint32_t i = i_vox.id;
                    u32_voxel = u32_voxel | (r << 0x00);
                    u32_voxel = u32_voxel | (g << 0x08);
                    u32_voxel = u32_voxel | (b << 0x10);
                    u32_voxel = u32_voxel | (i << 0x18);

                    tile_set.insert(u32_voxel);
                }
            }
        }

        size_t variants = tile_set.size();
        size_t bits_per_variant = ceil_log2(variants);

        size_t size = 0;

        // info (contains whether there's a palette chunk)
        uint32_t chunk_info = static_cast<uint32_t>(variants);
        size += sizeof(chunk_info);

        // insert palette
        size += sizeof(uint32_t) * variants;

        if (variants != 1) {
            // insert palette chunk
            auto palette_chunk_size = (CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE * bits_per_variant) / 8;
            palette_chunk_size = (palette_chunk_size + 3) / 4;
            size += palette_chunk_size * 4 + 1;
        }
        // WHY???
        size += 4;

        size_t old_size = data.size();
        data.reserve(old_size + size);
        for (size_t i = 0; i < size; ++i)
            data.push_back(0);

        uint8_t *output_buffer = data.data() + old_size;
        write_data<uint32_t>(output_buffer, chunk_info);

        uint32_t *palette_begin = (uint32_t *)output_buffer;
        uint32_t *palette_end = palette_begin + variants;

        size_t vi = 0;
        for (auto u32_voxel : tile_set) {
            write_data<uint32_t>(output_buffer, u32_voxel);

            // float r = float((u32_voxel >> 0x00) & 0xff) * 1.0f / 255.0f;
            // float g = float((u32_voxel >> 0x08) & 0xff) * 1.0f / 255.0f;
            // float b = float((u32_voxel >> 0x10) & 0xff) * 1.0f / 255.0f;
            // uint32_t i = (u32_voxel >> 0x18) & 0xff;
            // std::cout << "rgbi: " << r << " " << g << " " << b << " " << i << " ";
            // std::cout << "vi: " << vi << std::endl;
            ++vi;
        }
        // std::cout << "variants: " << variants << std::endl;

        if (variants > 1) {
            for (size_t zi = 0; zi < CHUNK_SIZE; ++zi) {
                for (size_t yi = 0; yi < CHUNK_SIZE; ++yi) {
                    for (size_t xi = 0; xi < CHUNK_SIZE; ++xi) {
                        // TODO: fix for non-multiple sizes of CHUNK_SIZE
                        size_t px = ox + xi;
                        size_t py = oy + yi;
                        size_t pz = oz + zi;
                        size_t index = px + py * node.size_x + pz * node.size_x * node.size_y;
                        auto const &i_vox = node.voxels[index];
                        uint32_t u32_voxel = 0;
                        uint32_t r = static_cast<uint32_t>(std::max(std::min(i_vox.color.x, 1.0f), 0.0f) * 255.0f);
                        uint32_t g = static_cast<uint32_t>(std::max(std::min(i_vox.color.y, 1.0f), 0.0f) * 255.0f);
                        uint32_t b = static_cast<uint32_t>(std::max(std::min(i_vox.color.z, 1.0f), 0.0f) * 255.0f);
                        uint32_t i = i_vox.id;
                        u32_voxel = u32_voxel | (r << 0x00);
                        u32_voxel = u32_voxel | (g << 0x08);
                        u32_voxel = u32_voxel | (b << 0x10);
                        u32_voxel = u32_voxel | (i << 0x18);
                        auto palette_iter = std::find(palette_begin, palette_end, u32_voxel);
                        uint32_t palette_id = static_cast<uint32_t>(palette_iter - palette_begin);
                        uint32_t bit_index = static_cast<uint32_t>(index * bits_per_variant);
                        uint32_t byte_index = bit_index / 8;
                        uint32_t bit_offset = bit_index - byte_index * 8;
                        uint32_t mask = mask_bases[bits_per_variant];
                        // std::cout << "bi: " << byte_index << "  " << std::flush;
                        auto &output = *reinterpret_cast<uint32_t *>(output_buffer + byte_index);
                        output = output & ~(mask << bit_offset);
                        output = output | (palette_id << bit_offset);
                    }
                    // std::cout << std::endl;
                }
            }
        }

        return size;
    }

    size_t node_precomp(GVoxSceneNode const &node) {
        size_t size = 0;

        size_t old_size = data.size();

        // for the size xyz
        size += sizeof(size_t) * 3;

        assert((node.size_x % CHUNK_SIZE) == 0);
        assert((node.size_y % CHUNK_SIZE) == 0);
        assert((node.size_z % CHUNK_SIZE) == 0);

        // these can be inferred and therefore don't need to be stored
        size_t chunk_nx = (node.size_x + CHUNK_SIZE - 1) / CHUNK_SIZE;
        size_t chunk_ny = (node.size_y + CHUNK_SIZE - 1) / CHUNK_SIZE;
        size_t chunk_nz = (node.size_z + CHUNK_SIZE - 1) / CHUNK_SIZE;

        // for the node's whole size
        size += sizeof(size_t) * 1;

        data.reserve(old_size + size);
        for (size_t i = 0; i < size; ++i)
            data.push_back(0);

        {
            uint8_t *output_buffer = data.data() + old_size;
            write_data<size_t>(output_buffer, node.size_x);
            write_data<size_t>(output_buffer, node.size_y);
            write_data<size_t>(output_buffer, node.size_z);
        }

        for (size_t zi = 0; zi < chunk_nz; ++zi) {
            for (size_t yi = 0; yi < chunk_ny; ++yi) {
                for (size_t xi = 0; xi < chunk_nx; ++xi) {
                    size += chunk_variance(node, xi, yi, zi);
                }
            }
        }

        {
            uint8_t *output_buffer = data.data() + old_size + sizeof(size_t) * 3;
            write_data<size_t>(output_buffer, size - sizeof(size_t) * 4);
        }

        // std::cout << size << std::endl;

        return size;
    }

    GVoxPayload create(GVoxScene const &scene) {
        GVoxPayload result = {};
        result.size += sizeof(size_t);
        auto pre_node_size = result.size;
        for (size_t node_i = 0; node_i < scene.node_n; ++node_i) {
            if (!scene.nodes[node_i].voxels)
                continue;
            result.size += node_precomp(scene.nodes[node_i]);
        }
        result.data = new uint8_t[result.size];
        auto *buffer_ptr = result.data;
        write_data<size_t>(buffer_ptr, scene.node_n);
        memcpy(buffer_ptr, data.data(), result.size - pre_node_size);
        return result;
    }
};

struct Context {
    Context();
    ~Context();

    GVoxPayload create_payload(GVoxScene scene);
    void destroy_payload(GVoxPayload payload);
    GVoxScene parse_payload(GVoxPayload payload);
};

Context::Context() {
}

Context::~Context() {
}

GVoxPayload Context::create_payload(GVoxScene scene) {
    PaletteCompressor compressor;
    return compressor.create(scene);
}

void Context::destroy_payload(GVoxPayload payload) {
    delete[] payload.data;
}

GVoxScene Context::parse_payload(GVoxPayload payload) {
    GVoxScene result = {};
    uint8_t *buffer_ptr = (uint8_t *)payload.data;
    result.node_n = read_data<size_t>(buffer_ptr);
    // std::cout << "node_n = " << result.node_n << std::endl;

    result.nodes = (GVoxSceneNode *)malloc(sizeof(GVoxSceneNode) * result.node_n);
    for (size_t node_i = 0; node_i < result.node_n; ++node_i) {
        auto &node = result.nodes[node_i];
        node.size_x = read_data<size_t>(buffer_ptr);
        node.size_y = read_data<size_t>(buffer_ptr);
        node.size_z = read_data<size_t>(buffer_ptr);
        auto total_size = read_data<size_t>(buffer_ptr);
        auto next_node = buffer_ptr + total_size;
        // std::cout << "imported " << node.size_x << " " << node.size_y << " " << node.size_z << std::endl;
        // std::cout << "node_size = " << total_size << std::endl;
        size_t voxels_n = result.nodes[node_i].size_x * result.nodes[node_i].size_y * result.nodes[node_i].size_z;
        size_t voxels_size = voxels_n * sizeof(GVoxVoxel);
        size_t chunk_nx = (node.size_x + CHUNK_SIZE - 1) / CHUNK_SIZE;
        size_t chunk_ny = (node.size_y + CHUNK_SIZE - 1) / CHUNK_SIZE;
        size_t chunk_nz = (node.size_z + CHUNK_SIZE - 1) / CHUNK_SIZE;
        node.voxels = (GVoxVoxel *)malloc(voxels_size);
        for (size_t chunk_z = 0; chunk_z < chunk_nz; ++chunk_z) {
            for (size_t chunk_y = 0; chunk_y < chunk_ny; ++chunk_y) {
                for (size_t chunk_x = 0; chunk_x < chunk_nx; ++chunk_x) {
                    size_t ox = chunk_x * CHUNK_SIZE;
                    size_t oy = chunk_y * CHUNK_SIZE;
                    size_t oz = chunk_z * CHUNK_SIZE;
                    auto chunk_info = read_data<uint32_t>(buffer_ptr);
                    // for now, the only info encoded inside the `chunk_info` variable
                    // is the number of variants inside the palette.
                    auto variants = chunk_info;
                    uint32_t *palette_begin = (uint32_t *)buffer_ptr;
                    size_t bits_per_variant = ceil_log2(variants);
                    assert(bits_per_variant < 9);
                    buffer_ptr += variants * sizeof(uint32_t);
                    if (variants == 1) {
                        for (size_t zi = 0; zi < CHUNK_SIZE; ++zi) {
                            for (size_t yi = 0; yi < CHUNK_SIZE; ++yi) {
                                for (size_t xi = 0; xi < CHUNK_SIZE; ++xi) {
                                    size_t px = ox + xi;
                                    size_t py = oy + yi;
                                    size_t pz = oz + zi;
                                    size_t index = px + py * node.size_x + pz * node.size_x * node.size_y;
                                    uint32_t u32_voxel = palette_begin[0];
                                    float r = ((u32_voxel >> 0x00) & 0xff) * 1.0f / 255.0f;
                                    float g = ((u32_voxel >> 0x08) & 0xff) * 1.0f / 255.0f;
                                    float b = ((u32_voxel >> 0x10) & 0xff) * 1.0f / 255.0f;
                                    uint32_t i = (u32_voxel >> 0x18) & 0xff;
                                    node.voxels[index] = GVoxVoxel{
                                        .color = {r, g, b},
                                        .id = i,
                                    };
                                }
                            }
                        }
                    } else {
                        for (size_t zi = 0; zi < CHUNK_SIZE; ++zi) {
                            for (size_t yi = 0; yi < CHUNK_SIZE; ++yi) {
                                for (size_t xi = 0; xi < CHUNK_SIZE; ++xi) {
                                    size_t px = ox + xi;
                                    size_t py = oy + yi;
                                    size_t pz = oz + zi;
                                    size_t index = px + py * node.size_x + pz * node.size_x * node.size_y;
                                    uint32_t bit_index = static_cast<uint32_t>(index * bits_per_variant);
                                    uint32_t byte_index = bit_index / 8;
                                    uint32_t bit_offset = bit_index - byte_index * 8;
                                    uint32_t mask = mask_bases[bits_per_variant];
                                    auto &input = *reinterpret_cast<uint32_t *>(buffer_ptr + byte_index);
                                    uint32_t palette_id = (input & mask) >> bit_offset;
                                    uint32_t u32_voxel = palette_begin[palette_id];
                                    float r = ((u32_voxel >> 0x00) & 0xff) * 1.0f / 255.0f;
                                    float g = ((u32_voxel >> 0x08) & 0xff) * 1.0f / 255.0f;
                                    float b = ((u32_voxel >> 0x10) & 0xff) * 1.0f / 255.0f;
                                    uint32_t i = (u32_voxel >> 0x18) & 0xff;
                                    node.voxels[index] = GVoxVoxel{
                                        .color = {r, g, b},
                                        .id = i,
                                    };
                                }
                            }
                        }
                    }
                }
            }
        }
        buffer_ptr = next_node;
    }

    return result;
}

extern "C" EXPORT void *gvox_format_create_context() {
    auto result = new Context{};
    return result;
}

extern "C" EXPORT void gvox_format_destroy_context(void *context_ptr) {
    auto self = reinterpret_cast<Context *>(context_ptr);
    delete self;
}

extern "C" EXPORT GVoxPayload gvox_format_create_payload(void *context_ptr, GVoxScene scene) {
    auto self = reinterpret_cast<Context *>(context_ptr);
    return self->create_payload(scene);
}

extern "C" EXPORT void gvox_format_destroy_payload(void *context_ptr, GVoxPayload payload) {
    auto self = reinterpret_cast<Context *>(context_ptr);
    self->destroy_payload(payload);
}

extern "C" EXPORT GVoxScene gvox_format_parse_payload(void *context_ptr, GVoxPayload payload) {
    auto self = reinterpret_cast<Context *>(context_ptr);
    return self->parse_payload(payload);
}
