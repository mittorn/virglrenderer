#ifndef VIRGL_HW_H
#define VIRGL_HW_H

typedef uint64_t VIRGLPHYSICAL;
/* specification for the HW command processor */

struct virgl_box {
	uint32_t x, y, z;
	uint32_t w, h, d;
};

enum virgl_cmd_type {
	VIRGL_CMD_NOP,
	VIRGL_CMD_CREATE_CONTEXT,
	VIRGL_CMD_CREATE_RESOURCE,
	VIRGL_CMD_SUBMIT,
	VIRGL_CMD_DESTROY_CONTEXT,
	VIRGL_CMD_TRANSFER_GET,
	VIRGL_CMD_TRANSFER_PUT,
	VIRGL_CMD_SET_SCANOUT,
	VIRGL_CMD_FLUSH_BUFFER,
	VIRGL_CMD_RESOURCE_UNREF,
	VIRGL_CMD_ATTACH_RES_CTX,
	VIRGL_CMD_DETACH_RES_CTX,
        VIRGL_CMD_RESOURCE_ATTACH_SG_LIST,
        VIRGL_CMD_RESOURCE_INVALIDATE_SG_LIST,
        VIRGL_CMD_GET_3D_CAPABILITIES,
        VIRGL_CMD_TIMESTAMP_GET,
};

/* put a box of data from a BO into a tex/buffer resource */
struct virgl_transfer_put {
	VIRGLPHYSICAL data;
	uint32_t res_handle;
	struct virgl_box box;
	uint32_t level;
	uint32_t stride;
	uint32_t layer_stride;
	uint32_t ctx_id;
};

struct virgl_transfer_get {
	VIRGLPHYSICAL data;
	uint32_t res_handle;
	struct virgl_box box;
	int level;
	uint32_t stride;
	uint32_t layer_stride;
	uint32_t ctx_id;
};

struct virgl_flush_buffer {
	uint32_t res_handle;
        uint32_t ctx_id;
	struct virgl_box box;
};

struct virgl_set_scanout {
	uint32_t res_handle;
        uint32_t ctx_id;
	struct virgl_box box;
};

/* is 0,0 for this resource at the top or the bottom?
   kernel console and X want this, 3D driver doesn't.
   this flag should only be used with formats that are
   renderable. otherwise the context will get locked up.
*/
#define VIRGL_RESOURCE_Y_0_TOP (1 << 0)
struct virgl_resource_create {
	uint32_t handle;
	uint32_t target;
	uint32_t format;
	uint32_t bind;
	uint32_t width;
	uint32_t height;
	uint32_t depth;
	uint32_t array_size;
	uint32_t last_level;
	uint32_t nr_samples;
        uint32_t nr_sg_entries;
        uint32_t flags;
};

struct virgl_resource_unref {
	uint32_t res_handle;
};

struct virgl_cmd_submit {
	uint64_t phy_addr;
	uint32_t size;
        uint32_t ctx_id;
};

struct virgl_cmd_context {
        uint32_t handle;
        uint32_t pad;
};

struct virgl_cmd_context_create {
        uint32_t handle;
        uint32_t nlen;
        char debug_name[64];
};

struct virgl_cmd_resource_context {
	uint32_t resource;
	uint32_t ctx_id;
};

struct virgl_cmd_resource_attach_sg {
        uint32_t resource;
        uint32_t num_sg_entries;
};

struct virgl_cmd_resource_invalidate_sg {
        uint32_t resource;
};

struct virgl_cmd_get_cap {
        uint32_t cap_set;
        uint32_t cap_set_version;
        VIRGLPHYSICAL offset;
};

struct virgl_iov_entry {
	VIRGLPHYSICAL addr;
	uint32_t length;
	uint32_t pad;
};

struct virgl_cmd_timestamp_get {
        uint64_t timestamp;
};

#define VIRGL_COMMAND_EMIT_FENCE (1 << 0)

struct virgl_command {
	uint32_t type;
	uint32_t flags;
	uint64_t fence_id;
	union virgl_cmds {
		struct virgl_cmd_context ctx;
		struct virgl_cmd_context_create ctx_create;
		struct virgl_resource_create res_create;
		struct virgl_transfer_put transfer_put;
		struct virgl_transfer_get transfer_get;
		struct virgl_cmd_submit cmd_submit;
		struct virgl_set_scanout set_scanout;
		struct virgl_flush_buffer flush_buffer;
		struct virgl_resource_unref res_unref;
		struct virgl_cmd_resource_context res_ctx;

                struct virgl_cmd_resource_attach_sg attach_sg;
                struct virgl_cmd_resource_invalidate_sg inval_sg;
                struct virgl_cmd_get_cap get_cap;
                struct virgl_cmd_timestamp_get get_timestamp;
	} u;
};

/* formats known by the HW device - based on gallium subset */
enum virgl_formats {
   VIRGL_FORMAT_B8G8R8A8_UNORM          = 1,
   VIRGL_FORMAT_B8G8R8X8_UNORM          = 2,
   VIRGL_FORMAT_A8R8G8B8_UNORM          = 3,
   VIRGL_FORMAT_X8R8G8B8_UNORM          = 4,
   VIRGL_FORMAT_B5G5R5A1_UNORM          = 5,
   VIRGL_FORMAT_B4G4R4A4_UNORM          = 6,
   VIRGL_FORMAT_B5G6R5_UNORM            = 7,
   VIRGL_FORMAT_L8_UNORM                = 9,    /**< ubyte luminance */
   VIRGL_FORMAT_A8_UNORM                = 10,   /**< ubyte alpha */
   VIRGL_FORMAT_L8A8_UNORM              = 12,   /**< ubyte alpha, luminance */
   VIRGL_FORMAT_L16_UNORM               = 13,   /**< ushort luminance */

   VIRGL_FORMAT_Z16_UNORM               = 16,
   VIRGL_FORMAT_Z32_UNORM               = 17,
   VIRGL_FORMAT_Z32_FLOAT               = 18,
   VIRGL_FORMAT_Z24_UNORM_S8_UINT       = 19,
   VIRGL_FORMAT_S8_UINT_Z24_UNORM       = 20,
   VIRGL_FORMAT_Z24X8_UNORM             = 21,
   VIRGL_FORMAT_S8_UINT                 = 23,   /**< ubyte stencil */

   VIRGL_FORMAT_R32_FLOAT               = 28,
   VIRGL_FORMAT_R32G32_FLOAT            = 29,
   VIRGL_FORMAT_R32G32B32_FLOAT         = 30,
   VIRGL_FORMAT_R32G32B32A32_FLOAT      = 31,

   VIRGL_FORMAT_R16_UNORM               = 48,
   VIRGL_FORMAT_R16G16_UNORM            = 49,

   VIRGL_FORMAT_R16G16B16A16_UNORM      = 51,

   VIRGL_FORMAT_R16_SNORM               = 56,
   VIRGL_FORMAT_R16G16_SNORM            = 57,
   VIRGL_FORMAT_R16G16B16A16_SNORM      = 59,

   VIRGL_FORMAT_R8_UNORM                = 64,
   VIRGL_FORMAT_R8G8_UNORM              = 65,

   VIRGL_FORMAT_R8G8B8A8_UNORM          = 67,

   VIRGL_FORMAT_R8_SNORM                = 74,
   VIRGL_FORMAT_R8G8_SNORM              = 75,
   VIRGL_FORMAT_R8G8B8_SNORM            = 76,
   VIRGL_FORMAT_R8G8B8A8_SNORM          = 77,

   VIRGL_FORMAT_R16_FLOAT               = 91,
   VIRGL_FORMAT_R16G16_FLOAT            = 92,
   VIRGL_FORMAT_R16G16B16_FLOAT         = 93,
   VIRGL_FORMAT_R16G16B16A16_FLOAT      = 94,

   VIRGL_FORMAT_L8_SRGB                 = 95,
   VIRGL_FORMAT_L8A8_SRGB               = 96,
   VIRGL_FORMAT_B8G8R8A8_SRGB           = 100,
   VIRGL_FORMAT_B8G8R8X8_SRGB           = 101,

   /* compressed formats */
   VIRGL_FORMAT_DXT1_RGB                = 105,
   VIRGL_FORMAT_DXT1_RGBA               = 106,
   VIRGL_FORMAT_DXT3_RGBA               = 107,
   VIRGL_FORMAT_DXT5_RGBA               = 108,

   /* sRGB, compressed */
   VIRGL_FORMAT_DXT1_SRGB               = 109,
   VIRGL_FORMAT_DXT1_SRGBA              = 110,
   VIRGL_FORMAT_DXT3_SRGBA              = 111,
   VIRGL_FORMAT_DXT5_SRGBA              = 112,

   /* rgtc compressed */
   VIRGL_FORMAT_RGTC1_UNORM             = 113,
   VIRGL_FORMAT_RGTC1_SNORM             = 114,
   VIRGL_FORMAT_RGTC2_UNORM             = 115,
   VIRGL_FORMAT_RGTC2_SNORM             = 116,

   VIRGL_FORMAT_A8B8G8R8_UNORM          = 121,
   VIRGL_FORMAT_B5G5R5X1_UNORM          = 122,
   VIRGL_FORMAT_R11G11B10_FLOAT         = 124,
   VIRGL_FORMAT_R9G9B9E5_FLOAT          = 125,
   VIRGL_FORMAT_Z32_FLOAT_S8X24_UINT    = 126,

   VIRGL_FORMAT_B10G10R10A2_UNORM       = 131,
   VIRGL_FORMAT_R8G8B8X8_UNORM          = 134,
   VIRGL_FORMAT_B4G4R4X4_UNORM          = 135,
   VIRGL_FORMAT_B2G3R3_UNORM            = 139,

   VIRGL_FORMAT_L16A16_UNORM            = 140,
   VIRGL_FORMAT_A16_UNORM               = 141,

   VIRGL_FORMAT_A8_SNORM                = 147,
   VIRGL_FORMAT_L8_SNORM                = 148,
   VIRGL_FORMAT_L8A8_SNORM              = 149,

   VIRGL_FORMAT_A16_SNORM               = 151,
   VIRGL_FORMAT_L16_SNORM               = 152,
   VIRGL_FORMAT_L16A16_SNORM            = 153,

   VIRGL_FORMAT_A16_FLOAT               = 155,
   VIRGL_FORMAT_L16_FLOAT               = 156,
   VIRGL_FORMAT_L16A16_FLOAT            = 157,

   VIRGL_FORMAT_A32_FLOAT               = 159,
   VIRGL_FORMAT_L32_FLOAT               = 160,
   VIRGL_FORMAT_L32A32_FLOAT            = 161,

   VIRGL_FORMAT_R8_UINT                 = 177,
   VIRGL_FORMAT_R8G8_UINT               = 178,
   VIRGL_FORMAT_R8G8B8_UINT             = 179,
   VIRGL_FORMAT_R8G8B8A8_UINT           = 180,

   VIRGL_FORMAT_R8_SINT                 = 181,
   VIRGL_FORMAT_R8G8_SINT               = 182,
   VIRGL_FORMAT_R8G8B8_SINT             = 183,
   VIRGL_FORMAT_R8G8B8A8_SINT           = 184,

   VIRGL_FORMAT_R16_UINT                = 185,
   VIRGL_FORMAT_R16G16_UINT             = 186,
   VIRGL_FORMAT_R16G16B16_UINT          = 187,
   VIRGL_FORMAT_R16G16B16A16_UINT       = 188,

   VIRGL_FORMAT_R16_SINT                = 189,
   VIRGL_FORMAT_R16G16_SINT             = 190,
   VIRGL_FORMAT_R16G16B16_SINT          = 191,
   VIRGL_FORMAT_R16G16B16A16_SINT       = 192,
   VIRGL_FORMAT_R32_UINT                = 193,
   VIRGL_FORMAT_R32G32_UINT             = 194,
   VIRGL_FORMAT_R32G32B32_UINT          = 195,
   VIRGL_FORMAT_R32G32B32A32_UINT       = 196,

   VIRGL_FORMAT_R32_SINT                = 197,
   VIRGL_FORMAT_R32G32_SINT             = 198,
   VIRGL_FORMAT_R32G32B32_SINT          = 199,
   VIRGL_FORMAT_R32G32B32A32_SINT       = 200,

   VIRGL_FORMAT_A8_UINT                 = 201,
   VIRGL_FORMAT_L8_UINT                 = 203,
   VIRGL_FORMAT_L8A8_UINT               = 204,

   VIRGL_FORMAT_A8_SINT                 = 205,
   VIRGL_FORMAT_L8_SINT                 = 207,
   VIRGL_FORMAT_L8A8_SINT               = 208,

   VIRGL_FORMAT_A16_UINT                = 209,
   VIRGL_FORMAT_L16_UINT                = 211,
   VIRGL_FORMAT_L16A16_UINT             = 212,

   VIRGL_FORMAT_A16_SINT                = 213,
   VIRGL_FORMAT_L16_SINT                = 215,
   VIRGL_FORMAT_L16A16_SINT             = 216,

   VIRGL_FORMAT_A32_UINT                = 217,
   VIRGL_FORMAT_L32_UINT                = 219,
   VIRGL_FORMAT_L32A32_UINT             = 220,

   VIRGL_FORMAT_A32_SINT                = 221,
   VIRGL_FORMAT_L32_SINT                = 223,
   VIRGL_FORMAT_L32A32_SINT             = 224,

   VIRGL_FORMAT_B10G10R10A2_UINT        = 225, 
   VIRGL_FORMAT_R8G8B8X8_SNORM          = 229,

   VIRGL_FORMAT_R8G8B8X8_SRGB           = 230,

   VIRGL_FORMAT_B10G10R10X2_UNORM       = 233,
   VIRGL_FORMAT_R16G16B16X16_UNORM      = 234,
   VIRGL_FORMAT_R16G16B16X16_SNORM      = 235,
   VIRGL_FORMAT_MAX,
};

struct virgl_caps_bool_set1 {
        unsigned indep_blend_enable:1;
        unsigned indep_blend_func:1;
        unsigned cube_map_array:1;
        unsigned shader_stencil_export:1;
        unsigned conditional_render:1;
        unsigned start_instance:1;
        unsigned primitive_restart:1;
        unsigned blend_eq_sep:1;
        unsigned instanceid:1;
        unsigned vertex_element_instance_divisor:1;
        unsigned seamless_cube_map:1;
        unsigned occlusion_query:1;
        unsigned timer_query:1;
        unsigned streamout_pause_resume:1;
        unsigned texture_buffer_object:1;
        unsigned texture_multisample:1;
        unsigned fragment_coord_conventions:1;
};

/* endless expansion capabilites - current gallium has 252 formats */
struct virgl_supported_format_mask {
        uint32_t bitmask[16];
};
/* capabilities set 2 - version 1 - 32-bit and float values */
struct virgl_caps_v1 {
        uint32_t max_version;
        struct virgl_supported_format_mask sampler;
        struct virgl_supported_format_mask render;
        struct virgl_supported_format_mask depthstencil;
        struct virgl_supported_format_mask vertexbuffer;
        struct virgl_caps_bool_set1 bset;
        uint32_t glsl_level;
        uint32_t max_texture_array_layers;
        uint32_t max_streamout_buffers;
        uint32_t max_dual_source_render_targets;
        uint32_t max_render_targets;
        uint32_t max_samples;
};

union virgl_caps {
        uint32_t max_version;
        struct virgl_caps_v1 v1;
};

enum virgl_errors {
        VIRGL_ERROR_NONE,
        VIRGL_ERROR_UNKNOWN,
        VIRGL_ERROR_UNKNOWN_RESOURCE_FORMAT,
};

enum virgl_ctx_errors {
        VIRGL_ERROR_CTX_NONE,
        VIRGL_ERROR_CTX_UNKNOWN,
        VIRGL_ERROR_CTX_ILLEGAL_SHADER,
        VIRGL_ERROR_CTX_ILLEGAL_HANDLE,
        VIRGL_ERROR_CTX_ILLEGAL_RESOURCE,
        VIRGL_ERROR_CTX_ILLEGAL_SURFACE,
        VIRGL_ERROR_CTX_ILLEGAL_VERTEX_FORMAT,
};

#endif
