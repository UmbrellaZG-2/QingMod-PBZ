#pragma once
#include <stdint.h>
#ifdef CERIS_CORE_IMPORT
#define CERIS_API __declspec(dllimport)
#else
#define CERIS_API __declspec(dllexport)
#endif
// Stable C ABI. Native PMX units; Saba's right-handed coordinates (x,y,-z).
// Matrices are column-major. Input quaternions are x,y,z,w.
struct CerisBoneInput { int32_t index; float translation[3]; float rotation[4]; };
struct CerisGlobalInput { int32_t index; float position[3]; float rotation[4]; uint32_t flags; int32_t anchor=-1; int32_t twist=-1; };
struct alignas(16) CerisUETransform { double rotation[4]; double translation[4]; double scale[4]; };
extern "C" {
CERIS_API void* ceris_create(const char* pmx_utf8);
CERIS_API void ceris_destroy(void* handle);
CERIS_API const char* ceris_error();
CERIS_API const char* ceris_inventory(void* handle);
CERIS_API int ceris_reset(void* handle);
CERIS_API int ceris_step(void* handle, const CerisBoneInput* bones, int bone_count,
                        const float* morphs, int morph_count, float seconds, int physics, int ik);
CERIS_API int ceris_step_global(void* handle, const CerisGlobalInput* bones, int count,
                               const float* morphs, int morph_count, float seconds, int reset_physics);
CERIS_API void* ceris_game_create(const char* pmx_utf8,const char* plan_utf8);
CERIS_API void ceris_game_destroy(void* handle);
CERIS_API int ceris_game_rigid_matrices(void* handle,float* output,int float_count);
CERIS_API int ceris_game_saba_matrices(void* handle,float* output,int float_count);
CERIS_API float ceris_game_grip_error_cm(void* handle);
CERIS_API int ceris_game_physics_rescues(void* handle);
CERIS_API int ceris_game_hair_calibrations(void* handle);
CERIS_API int ceris_game_pelvis_calibrations(void* handle);
// out[0]=max cloth drift (MMD units), out[1..3]=cloth bone centroid, out[4]=cloud radius,
// out[5]=cloth bone count, out[6]=cloth snap-backs, out[7]=worst cloth bone index.
CERIS_API int ceris_game_cloth_stats(void* handle, float* out, int n);
CERIS_API int ceris_game_step(void* handle,const CerisUETransform* source,int source_count,
                             const CerisUETransform* component_world,const float* morphs,int morph_count,
                             float seconds,CerisUETransform* output,int output_count);
CERIS_API int ceris_bone_matrices(void* handle, float* output, int float_count);
CERIS_API int ceris_vertices(void* handle, float* output, int float_count);
CERIS_API int ceris_rigid_matrices(void* handle, float* output, int float_count);
}
