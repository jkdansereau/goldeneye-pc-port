#include <ultra64.h>
#include "lightfixture.h"
#include "bg.h"
#include "glass.h"
#include <bondconstants.h>
#include <assets/image_externs.h>
#include <PR/gbi.h>

#define LIGHTFIXTURE_TABLE_MAX 0x64
#define DARKENED_LIGHT_TABLE_MAX 0x200

// bss
//CODE.bss:80082660
s_lightfixture light_fixture_table[LIGHTFIXTURE_TABLE_MAX];
//CODE.bss:80082B10
s16 cur_entry_lightfixture_table;
//CODE.bss:80082B12
s16 index_of_cur_entry_lightfixture_table;
//CODE.bss:80082B14                     .align 3
//CODE.bss:80082B18
struct s_darkened_light darkened_light_table[DARKENED_LIGHT_TABLE_MAX]; // a table containing the vertices of lights that were shot, and therefore, darkened
//CODE.bss:80083318
s32 dword_CODE_bss_80083318;

// data
//D:80046030
s32 cur_entry_darkened_light_table = 0;

s32 D_80046034[] = {0, 0, 0, 0, 0, 0, 0};


void init_lightfixture_tables(void)
{
    s32 i;

    for (i = 0; i < LIGHTFIXTURE_TABLE_MAX; i++)
    {
        light_fixture_table[i].room_index = 0;
    }

    for (i = 0; i < DARKENED_LIGHT_TABLE_MAX; i++)
    {
        darkened_light_table[i].room_index = 0;
    }

    cur_entry_darkened_light_table = 0;
}


s32 get_index_of_current_entry_in_init_lightfixture_table(void)
{
    s32 i;

    for (i = 0; i != LIGHTFIXTURE_TABLE_MAX; i++)
    {
        if (light_fixture_table[i].room_index == 0)
        {
            return i;
        }
    }
    return LIGHTFIXTURE_TABLE_MAX;
}


void add_entry_to_init_lightfixture_table(Gfx *DL)
{
    cur_entry_lightfixture_table = get_index_of_current_entry_in_init_lightfixture_table();
    if (cur_entry_lightfixture_table != LIGHTFIXTURE_TABLE_MAX)
    {
        light_fixture_table[cur_entry_lightfixture_table].room_index = index_of_cur_entry_lightfixture_table;
        light_fixture_table[cur_entry_lightfixture_table].ptr_start_pertinent_DL = DL;
    }
}


void save_ptrDL_enpoint_to_current_init_lightfixture_table(Gfx *param_1)
{
    if (cur_entry_lightfixture_table != LIGHTFIXTURE_TABLE_MAX)
    {
        light_fixture_table[cur_entry_lightfixture_table].ptr_end_pertinent_DL = param_1;
    }
}


s32 check_if_imageID_is_light(s32 imageID)
{
    if ((imageID == IMAGE_WALL_LAMP)     ||
        (imageID == IMAGE_203_LIGHT)     ||
        (imageID == IMAGE_205_LIGHT)     ||
        (imageID == IMAGE_252_LIGHT)     ||
        (imageID == IMAGE_PANEL_LAMP)    ||
        (imageID == IMAGE_255_LIGHT)     ||
        (imageID == IMAGE_256_LIGHT)     ||
        (imageID == IMAGE_HANGING_LAMP)  ||
        (imageID == IMAGE_NEON_LAMP)     ||
        (imageID == IMAGE_LINEAR_LAMP))
    {
        // Will darken when shot
        return 1;
    } 
    else
    {
        return 0;
    }
}


Vtx * return_ptr_vertex_of_entry_room(Gfx * gfx, s32 room_index)
{
    Vtx * ret;

    while (gfx->dma.cmd != G_VTX ){ gfx--; }

    ret = gfx->dma.addr;

    // weird memory checking, not sure what's going on here
    if (((s32) ret & 0xFF000000) == 0x0E000000) {
        ret = (s32)g_BgRoomInfo[room_index].ptr_point_index + ((s32) ret & 0xFFFFFF);
    }

    return ret;
}


void extract_vertex_indices_from_triangle(Gfx* gfx, u32 tri_type, s32* idx1, s32* idx2, s32* idx3)
{
    switch (tri_type) {
        case 0:
            *idx1 = (s32) gfx->tri.tri.v[0] / 10;
            *idx2 = (s32) gfx->tri.tri.v[1] / 10;
            *idx3 = (s32) gfx->tri.tri.v[2] / 10;
            break;
        // unsure of how to cleanly access the below versions
        case 1:
            *idx1 = ((u32*)gfx)[1] & 0xF;
            *idx2 = ((((u8*)gfx)[7]) & 0xFFFFFFFFu) >> 4;
            *idx3 = ((s32*)gfx)[0] & 0xF;
            break;
        case 2:
            *idx1 = ((u8*)gfx)[6] & 0xF;
            *idx2 = ((((u16*)gfx)[3]) & 0xFFFFFFFFu) >> 0xC;
            *idx3 = ((((u8*)gfx)[3]) & 0xFFFFFFFFu) >> 4;
            break;
        case 3:
            *idx1 = ((u16*)gfx)[2] & 0xF;
            *idx2 = ((((u8*)gfx)[5]) & 0xFFFFFFFFu) >> 4;
            *idx3 = ((u8*)gfx)[2] & 0xF;
            break;
        case 4:
            *idx1 = ((u8*)gfx)[4] & 0xF;
            *idx2 = ((u32*)gfx)[1] >> 0x1C;
            *idx3 = ((((u16*)gfx)[1]) & 0xFFFFFFFFu) >> 0xC;
            break;
    }
}


void extract_vertex_coords_from_triangle(Gfx * gfx, u32 tri_type, s32 room_index, coord16 * out1, coord16 * out2, coord16 * out3)
{
    s32 idx1;
    s32 idx2;
    s32 idx3;
    Vtx * vertices;

    extract_vertex_indices_from_triangle(gfx, tri_type, &idx1, &idx2, &idx3);
    vertices = return_ptr_vertex_of_entry_room(gfx, room_index);

    out1->AsArray[0] = (s16) vertices[idx1].v.ob[0];
    out1->AsArray[1] = (s16) vertices[idx1].v.ob[1];
    out1->AsArray[2] = (s16) vertices[idx1].v.ob[2];

    out2->AsArray[0] = (s16) vertices[idx2].v.ob[0];
    out2->AsArray[1] = (s16) vertices[idx2].v.ob[1];
    out2->AsArray[2] = (s16) vertices[idx2].v.ob[2];

    out3->AsArray[0] = (s16) vertices[idx3].v.ob[0];
    out3->AsArray[1] = (s16) vertices[idx3].v.ob[1];
    out3->AsArray[2] = (s16) vertices[idx3].v.ob[2];
}


void redarken_lights_in_room(s32 room_index)
{
    Vtx * vertex;
    s32 i;
    struct s_darkened_light* unk;

    vertex = g_BgRoomInfo[room_index].ptr_point_index;

    for (i = 0; i < DARKENED_LIGHT_TABLE_MAX; i++)
    {
        unk = &darkened_light_table[i];

        if (room_index != unk->room_index) { continue; }

        vertex[unk->vtx_index].v.cn[0] >>= 2;
        vertex[unk->vtx_index].v.cn[1] >>= 2;
        vertex[unk->vtx_index].v.cn[2] >>= 2;
        vertex[unk->vtx_index].v.cn[3] >>= 2;
    }
}


void darken_vertex_in_room(Vtx * vertex, s32 room_index)
{
    s32 vtx_index;

    // Check if this vertex was already darkened
    if (darkened_light_table_contains_vertex(vertex, room_index) != 0) { return; }

    // weird memory stuff going on here
    vtx_index = ((u32)vertex - (u32)g_BgRoomInfo[room_index].ptr_point_index) >> 4;

    darkened_light_table[cur_entry_darkened_light_table].room_index = (u16) room_index;
    darkened_light_table[cur_entry_darkened_light_table].vtx_index = vtx_index;

    vertex->v.cn[0] >>= 2;
    vertex->v.cn[1] >>= 2;
    vertex->v.cn[2] >>= 2;
    vertex->v.cn[3] >>= 2;

    cur_entry_darkened_light_table++;

    if (cur_entry_darkened_light_table >= DARKENED_LIGHT_TABLE_MAX)
    {
        cur_entry_darkened_light_table = 0;
    }
}


s32 darkened_light_table_contains_vertex(Vtx * vertex, s32 room_index)
{
    u32 vtx_index;
    s32 i;

    // weird memory stuff going on here
    vtx_index = ((u32)vertex - (u32)g_BgRoomInfo[room_index].ptr_point_index) >> 4;

    for (i = 0; i < DARKENED_LIGHT_TABLE_MAX; i++)
    {
        if ((room_index == darkened_light_table[i].room_index) && ((s32)vtx_index == darkened_light_table[i].vtx_index))
        {
            return TRUE;
        }
    }

    return FALSE;
}


void darken_triangle_in_room(Gfx *gfx, u32 tri_type, s32 room_index)
{
    Vtx * vertex;
    s32 idx1;
    s32 idx2;
    s32 idx3;
    Vtx * vertices;

    extract_vertex_indices_from_triangle(gfx, tri_type, &idx1, &idx2, &idx3);
    vertices = return_ptr_vertex_of_entry_room(gfx, room_index);

    darken_vertex_in_room(&vertices[idx1], room_index);
    darken_vertex_in_room(&vertices[idx2], room_index);

    vertex = &vertices[idx3];
    darken_vertex_in_room(vertex, room_index);
}


s32 darkened_light_table_contains_triangle(Gfx * gfx, u32 tri_type, s32 room_index)
{
    s32 out3;
    s32 idx1;
    s32 idx2;
    s32 idx3;
    Vtx * vertices;
    s32 out2;
    s32 out1;

    extract_vertex_indices_from_triangle(gfx, tri_type, &idx1, &idx2, &idx3);
    vertices = return_ptr_vertex_of_entry_room(gfx, room_index);
    out1 = darkened_light_table_contains_vertex(&vertices[idx2], room_index);
    out2 = darkened_light_table_contains_vertex(&vertices[idx1], room_index);
    out3 = darkened_light_table_contains_vertex(&vertices[idx3], room_index);
    return out3 + out2 + out1;
}

/**
 * Test whether a tri belongs to a light fixture region that should also be darkened.
 */
s32 lightIsCoordNearDarkenedVertex(coord16 * coord, s32 room_index)
{
    s32 dx;
    s32 dy;
    s32 dz;
    s32 i;
    Vtx * vertex;

    i = 0;
    do
    {
        if (room_index == darkened_light_table[i].room_index)
        {
            vertex = &g_BgRoomInfo[room_index].ptr_point_index[darkened_light_table[i].vtx_index];

            dx = vertex->v.ob[0] - coord->AsArray[0];
            dy = vertex->v.ob[1] - coord->AsArray[1];
            dz = vertex->v.ob[2] - coord->AsArray[2];

            if (dx < 0) { dx = -dx; }
            if (dy < 0) { dy = -dy; }
            if (dz < 0) { dz = -dz; }

            if ((dx + dy + dz) < (s32) (get_room_data_float1() * 100.0f))
            {
                return 1;
            }
        }
    } while (++i < DARKENED_LIGHT_TABLE_MAX);

    return 0;
}

/**
 * Darken the vertices belonging to a light fixture and spawn shards of glass.
 */
void lightFixtureBreak(Gfx * gfx, u32 tri_type, s32 room_index)
{
    s16 diff_z_12;

    // Vertices of the hit triangle
    coord16 hit_vtx1;
    coord16 hit_vtx2;
    coord16 hit_vtx3;

    coord16 tri_vtx1;
    coord16 tri_vtx2;
    coord16 tri_vtx3;

    s16 diff_x_13;
    s16 diff_x_12;
    Gfx *fixture_gfx;
    f32 dist_tween;
    s32 j;
    s8 should_darken1;
    s8 should_darken2;
    s16 diff_y_13;
    s16 diff_y_12;
    s16 diff_x_23;
    s16 diff_z_13;
    f32 inv_dist_12;
    f32 inv_dist_23;
    f32 inv_dist_13;
    coord3d room_origin;
    coord3d shard_pos;
    s32 i;
    s16 diff_z_23;
    f32 edge_length;
    s16 diff_y_23;

    for (i = 0; i < LIGHTFIXTURE_TABLE_MAX; i++)
    {
        if (room_index != light_fixture_table[i].room_index) { continue; }

        if (gfx < light_fixture_table[i].ptr_start_pertinent_DL) { continue; }
        if (gfx >= light_fixture_table[i].ptr_end_pertinent_DL) { continue; }

        if (darkened_light_table_contains_triangle(gfx, tri_type, light_fixture_table[i].room_index) != 0) { return; }

        /**
         * Darken the exact triangle that was shot.
         */
        darken_triangle_in_room(gfx, tri_type, light_fixture_table[i].room_index);

        /**
         * Get the hit tri's verts, compute the edge lengths, and a step size (inv_dist)
         */
        extract_vertex_coords_from_triangle(gfx, tri_type, light_fixture_table[i].room_index, &hit_vtx1, &hit_vtx2, &hit_vtx3);

		diff_x_12 = hit_vtx1.AsArray[0] - hit_vtx2.AsArray[0];
		diff_x_23 = hit_vtx1.AsArray[0] - hit_vtx3.AsArray[0];
		diff_x_13 = hit_vtx2.AsArray[0] - hit_vtx3.AsArray[0];

		diff_y_12 = hit_vtx1.AsArray[1] - hit_vtx2.AsArray[1];
		diff_y_23 = hit_vtx1.AsArray[1] - hit_vtx3.AsArray[1];
		diff_y_13 = hit_vtx2.AsArray[1] - hit_vtx3.AsArray[1];

		diff_z_12 = hit_vtx1.AsArray[2] - hit_vtx2.AsArray[2];
		diff_z_23 = hit_vtx1.AsArray[2] - hit_vtx3.AsArray[2];
		diff_z_13 = hit_vtx2.AsArray[2] - hit_vtx3.AsArray[2];

        edge_length = sqrtf((diff_x_12 * diff_x_12) + (diff_y_12 * diff_y_12) + (diff_z_12 * diff_z_12));
        inv_dist_12 = 10.0f / (get_room_data_float2() * edge_length);

        edge_length = sqrtf((diff_x_23 * diff_x_23) + (diff_y_23 * diff_y_23) + (diff_z_23 * diff_z_23));
        inv_dist_23 = 10.0f / (get_room_data_float2() * edge_length);

        edge_length = sqrtf((diff_x_13 * diff_x_13) + (diff_y_13 * diff_y_13) + (diff_z_13 * diff_z_13));
        inv_dist_13 = 10.0f / (get_room_data_float2() * edge_length);

        getRoomPositionScaledByIndex(light_fixture_table[i].room_index, &room_origin);

        /**
         * Spawn glass shards along the edges of the hit tri.
         * Shards are spawned at fixed length intervals i.e. a long edge spawns more shards than a short edge.
         * Positions are converted from room space to world space for the glassCreateShard() function.
         */
        for (dist_tween = 0.0f; dist_tween < 1.0f; dist_tween += inv_dist_12)
        {
            shard_pos.x = ((hit_vtx2.AsArray[0] + (diff_x_12 * dist_tween)) * get_room_data_float2()) + room_origin.f[0];
            shard_pos.y = ((hit_vtx2.AsArray[1] + (diff_y_12 * dist_tween)) * get_room_data_float2()) + room_origin.f[1];
            shard_pos.z = ((hit_vtx2.AsArray[2] + (diff_z_12 * dist_tween)) * get_room_data_float2()) + room_origin.f[2];
            glassCreateShard(&shard_pos, 0.0f, 10.0f);
        }

        for (dist_tween = 0.0f; dist_tween < 1.0f; dist_tween += inv_dist_23)
        {
            shard_pos.x = ((hit_vtx3.AsArray[0] + (diff_x_23 * dist_tween)) * get_room_data_float2()) + room_origin.f[0];
            shard_pos.y = ((hit_vtx3.AsArray[1] + (diff_y_23 * dist_tween)) * get_room_data_float2()) + room_origin.f[1];
            shard_pos.z = ((hit_vtx3.AsArray[2] + (diff_z_23 * dist_tween)) * get_room_data_float2()) + room_origin.f[2];
            glassCreateShard(&shard_pos, 0.0f, 10.0f);
        }

        for (dist_tween = 0.0f; dist_tween < 1.0f; dist_tween += inv_dist_13)
        {
            shard_pos.x = ((hit_vtx3.AsArray[0] + (diff_x_13 * dist_tween)) * get_room_data_float2()) + room_origin.f[0];
            shard_pos.y = ((hit_vtx3.AsArray[1] + (diff_y_13 * dist_tween)) * get_room_data_float2()) + room_origin.f[1];
            shard_pos.z = ((hit_vtx3.AsArray[2] + (diff_z_13 * dist_tween)) * get_room_data_float2()) + room_origin.f[2];
            glassCreateShard(&shard_pos, 0.0f, 10.0f);
        }

        /**
         * Iterate over all tris in the fixture's display list range.
         * If any vertex of a tri is close to a previously darkened vertex,
         * darken the entire tri. This ensures the entire fixture is darkened, not just the hit tri.
         */
        for (fixture_gfx = light_fixture_table[i].ptr_start_pertinent_DL; fixture_gfx < light_fixture_table[i].ptr_end_pertinent_DL; fixture_gfx++)
        {
            if (fixture_gfx->dma.cmd == G_TRI1)
            {
                should_darken1 = 0;

                extract_vertex_coords_from_triangle(fixture_gfx, 0U, light_fixture_table[i].room_index, &tri_vtx1, &tri_vtx2, &tri_vtx3);

                if (lightIsCoordNearDarkenedVertex(&tri_vtx1, light_fixture_table[i].room_index) != 0)
                {
                    should_darken1 = 1;
                }
                else if (lightIsCoordNearDarkenedVertex(&tri_vtx2, light_fixture_table[i].room_index) != 0)
                {
                    should_darken1 = 1;
                }
                else if (lightIsCoordNearDarkenedVertex(&tri_vtx3, light_fixture_table[i].room_index) != 0)
                {
                    should_darken1 = 1;
                }

                if (should_darken1 != 0)
                {
                    darken_triangle_in_room(fixture_gfx, 0U, light_fixture_table[i].room_index);
                }
            }
            else if (fixture_gfx->dma.cmd == -0x4f /* G_TRI2 ? */)
            {
                for (j = 0; j < 4; j++)
                {
                    should_darken2 = 0;

                    extract_vertex_coords_from_triangle(fixture_gfx, j + 1, light_fixture_table[i].room_index, &tri_vtx1, &tri_vtx2, &tri_vtx3);

                    if (lightIsCoordNearDarkenedVertex(&tri_vtx1, light_fixture_table[i].room_index) != 0)
                    {
                        should_darken2 = 1;
                    }
                    else if (lightIsCoordNearDarkenedVertex(&tri_vtx2, light_fixture_table[i].room_index) != 0)
                    {
                        should_darken2 = 1;
                    }
                    else if (lightIsCoordNearDarkenedVertex(&tri_vtx3, light_fixture_table[i].room_index) != 0)
                    {
                        should_darken2 = 1;
                    }

                    if (should_darken2 != 0)
                    {
                        darken_triangle_in_room(fixture_gfx, j + 1, light_fixture_table[i].room_index);
                    }
                }
            }
        }
        return;
    }
}


void clear_light_fixturetable_in_room(s32 room_index)
{
    s32 i;
    for (i = 0; i < LIGHTFIXTURE_TABLE_MAX; i++)
    {
        if (room_index == light_fixture_table[i].room_index)
        {
            light_fixture_table[i].room_index = 0;
        }
    }
    index_of_cur_entry_lightfixture_table = room_index;
}

