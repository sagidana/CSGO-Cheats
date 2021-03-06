#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <tchar.h>
#include <stdio.h>
#include <math.h>

#pragma comment (lib, "User32.lib")
#pragma comment (lib, "Gdi32.lib")

#define SCREEN_WIDTH    (1920)
#define SCREEN_HEIGHT   (1080)

// -------------------- Common --------------------

#define ASSERT_TO(x, label) do{\
        if (x){             \
            goto label;     \
        }                   \
    }while(0)

#define COL_SIZE (8)
void hexdump(   unsigned char* buf,
                unsigned int size){
    int i,j;
    for (i = 0; i < size / COL_SIZE; i++){
        for (j = 0; j < COL_SIZE; j++){
            printf("%02x ", buf[(i*COL_SIZE) + j]);
        }
        printf("\n");
    }

    // print rest if need be.
    if (size % COL_SIZE != 0){
        for (j = 0; j < size % COL_SIZE; j++){
            printf("%02x ", buf[(size / COL_SIZE) + j]);
        }
        printf("\n");
    }
}

// ------------------------------------------------

unsigned char* client_base = NULL;
unsigned char* engine_base = NULL;
HANDLE csgo_handle = NULL;
HDC dc_handle = NULL;

HANDLE open_csgo(){
    HANDLE h_proc = NULL;
    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(PROCESSENTRY32);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);

    if (Process32First(snapshot, &entry) == TRUE)
    {
        while (Process32Next(snapshot, &entry) == TRUE)
        {
            if (stricmp(entry.szExeFile, "csgo.exe") == 0)
            {  
                h_proc = OpenProcess(   PROCESS_ALL_ACCESS,
                                        FALSE, 
                                        entry.th32ProcessID);

                break;
            }
        }
    }
    CloseHandle(snapshot);

    return h_proc;
}

#define MAX_NUM_OF_MODULES (1024)
void* module_get_base(char* target_mod_name){
    HMODULE h_modules[MAX_NUM_OF_MODULES];
    DWORD cb_needed;
    int i;

    ASSERT_TO(  EnumProcessModules(
                    csgo_handle, 
                    h_modules, 
                    sizeof(h_modules), 
                    &cb_needed) == FALSE, 
                end);

    for (i = 0; i < cb_needed / sizeof(HMODULE); i++){
        TCHAR mod_name[MAX_PATH];
        if (GetModuleFileNameEx(csgo_handle,
                                h_modules[i],
                                mod_name,
                                sizeof(mod_name) / sizeof(TCHAR)
                                ) == 0) continue;

        if (strstr(mod_name, target_mod_name) == NULL) continue;

        return h_modules[i];
    }

end:
    return NULL;
}

int read(   void* from, 
            unsigned char* to, 
            unsigned int size){
    int num_read = 0;
    ASSERT_TO(  ReadProcessMemory(  csgo_handle,
                                    from,
                                    to,
                                    size,
                                    &num_read
                                    ) == FALSE,
                end);
end:
    return num_read;
}

int write(  void* to, 
            unsigned char* from, 
            unsigned int size){
    int num_written = 0;
    ASSERT_TO(  WriteProcessMemory( csgo_handle,
                                    to,
                                    from,
                                    size,
                                    &num_written
                ) == FALSE,
            end);
end:
    return num_written;
}


// -------------------- Offsets --------------------
// keep updated from: https://github.com/frk1/hazedumper/blob/master/csgo.hpp

// client.dll
#define OFFSET_ENTITY_LIST                      (0x4DA521C) // eseential
#define OFFSET_VIEW_MATRIX                      (0x4D96B34) // essential
#define OFFSET_GLOW_OBJECT_MANAGER              (0x52ED6A8)

// engine.dll
#define OFFSET_CLIENT_STATE                     (0x589FE4)  // eseential

// relative to client_state
#define OFFSET_GET_LOCAL_PLAYER                 (0x180)     // eseential


// relative to player
#define OFFSET_PLAYER_TEAM_NUM                  (0xF4)      // essential    // team of the player
#define OFFSET_PLAYER_DORMANT                   (0xED)      // essential    // is player real
#define OFFSET_PLAYER_VEC_ORIGIN                (0x138)     // essential    // location of the player
#define OFFSET_PLAYER_BONE_MATRIX               (0x26A8)    // essential    // the bone matrix of the player this is to get to the head
#define BONE_HEAD_ID (8)                                    // essentail    // the id of the head bone.
#define OFFSET_PLAYER_HEALTH                    (0x100)     // essential    // the health of the player

#define OFFSET_GLOW_INDEX                       (0xA438)    // the glow index of the player


// -------------------------------------------------

// -------------------- Helpers --------------------

// -------------------- Structs --------------------

struct glow_object_t{
    unsigned char* entity;
    float r; 
    float g; 
    float b; 
    float a; 
    unsigned char glow_alpha_capped;
    float glow_alpha_max_velocity;
    float glow_alpha_max;
    float glow_pulse_overdrive;
    unsigned char render_occluded;
    unsigned char render_unoccluded;
    unsigned char full_bloom;
    int pad_5;
    int glow_style;
    int split_screen_slot;
    int next_free_slot;
};

struct bone_matrix_t{
    char pad1[12];
    float x;        // the only field that we care
    char pad2[12];
    float y;        // the only field that we care
    char pad3[12];
    float z;        // the only field that we care
};

struct view_matrix_t{
    float matrix[16];
};

struct location_t{
    float x;
    float y;
    float z;
};

struct position_t{
    float x;
    float y;
};

// -------------------------------------------------

struct position_t game_to_screen(struct location_t location){
    int num;
    struct position_t position = {0};
    struct view_matrix_t matrix = {0};

    num = read( client_base + OFFSET_VIEW_MATRIX,
                &matrix,
                sizeof(matrix));
    ASSERT_TO(num == 0, end);

    float _x = matrix.matrix[0] * location.x + matrix.matrix[1] * location.y + matrix.matrix[2] * location.z + matrix.matrix[3];
    float _y = matrix.matrix[4] * location.x + matrix.matrix[5] * location.y + matrix.matrix[6] * location.z + matrix.matrix[7];
    float _z = matrix.matrix[12] * location.x + matrix.matrix[13] * location.y + matrix.matrix[14] * location.z + matrix.matrix[15];

    _x *= 1.f / _z;
    _y *= 1.f / _z;

    position.x = SCREEN_WIDTH * .5f; 
    position.y = SCREEN_HEIGHT * .5f;

    position.x += 0.5f * _x * SCREEN_WIDTH + 0.5f;
    position.y -= 0.5f * _y * SCREEN_HEIGHT + 0.5f;

end:
    return position;
}

unsigned char* get_player(int index){

    unsigned char* player = NULL;
    read(   client_base + OFFSET_ENTITY_LIST + (0x10 * index),
            &player,
            sizeof(player));

    return player;
}

unsigned char* get_glow_object_manager(){

    unsigned char* glow_object_manager = NULL;
    read(   client_base + OFFSET_GLOW_OBJECT_MANAGER,
            &glow_object_manager,
            sizeof(glow_object_manager));

    return glow_object_manager;
}

unsigned char* get_client_state(){

    unsigned char* client_state = NULL;
    read(   engine_base + OFFSET_CLIENT_STATE,
            &client_state,
            sizeof(client_state));


    return client_state;
}

unsigned char* get_local_player(){
    int offset = -1;
    unsigned char* local_player = NULL;
    unsigned char* client_state = get_client_state();
    ASSERT_TO(client_base == NULL, end);

    read(   client_state + OFFSET_GET_LOCAL_PLAYER,
            &offset,
            sizeof(offset));
    ASSERT_TO(offset == -1, end);

    local_player = get_player(offset);
end:
    return local_player;
}

int player_get_team(unsigned char* player){
    int team_num = -1;

    read(   player + OFFSET_PLAYER_TEAM_NUM,
            &team_num,
            sizeof(team_num));

    return team_num;
}

int player_get_health(unsigned char* player){
    int health = -1;

    read(   player + OFFSET_PLAYER_HEALTH,
            &health,
            sizeof(health));

    return health;
}

int player_get_glow_index(unsigned char* player){
    int glow_index = -1;

    read(   player + OFFSET_GLOW_INDEX,
            &glow_index,
            sizeof(glow_index));

    return glow_index;
}

int player_is_real(unsigned char* player){
    char dormant = 0;

    read(   player + OFFSET_PLAYER_DORMANT,
            &dormant,
            sizeof(dormant));

    return dormant == 0;
}

struct location_t player_get_head_location(unsigned char* player){
    struct location_t head_location = {
        .x = -1,
        .y = -1,
        .z = -1
    };

    struct bone_matrix_t bone_matrix;
    unsigned char* bone_base = NULL;

    read(   player + OFFSET_PLAYER_BONE_MATRIX,
            &bone_base,
            sizeof(bone_base));

    ASSERT_TO(bone_base == NULL, end);

    read(bone_base + (sizeof(struct bone_matrix_t) * BONE_HEAD_ID),
            &bone_matrix,
            sizeof(bone_matrix));

    head_location.x = bone_matrix.x;
    head_location.y = bone_matrix.y;
    head_location.z = bone_matrix.z;

end:
    return head_location;
}

struct location_t player_get_location(unsigned char* player){
    struct location_t location;
    read(player + OFFSET_PLAYER_VEC_ORIGIN, 
            &location, 
            sizeof(location));
    return location;
}

void draw_point(float x, float y, float width){
    HDC h_dc = GetDC(NULL);
    ASSERT_TO(h_dc == NULL, end);

    HPEN h_pen = CreatePen( PS_SOLID,
                            width,
                            RGB(    255,
                                    0,
                                    0)
                            );
    ASSERT_TO(h_pen == NULL, end_release_dc);

    HPEN h_o_pen = (HPEN) SelectObject(h_dc, h_pen);

    MoveToEx(h_dc, x, y, NULL);
    LineTo(h_dc, x, y+1);

    DeleteObject(SelectObject(h_dc, h_o_pen));
    DeleteObject(h_pen);

end_release_dc:
    ReleaseDC(NULL, h_dc);
end:
    return;
}

void draw_enemy(struct position_t pos,
                float enemy_distance){

    float head_size = 3;
    head_size = head_size * 4000 / enemy_distance;
    draw_point(pos.x, pos.y, head_size);
}

float distance( float x_1, 
                float y_1, 
                float x_2, 
                float y_2){
    return sqrt(pow(x_2 - x_1, 2) + pow(y_2 - y_1, 2));
}

void glow_enenmy(unsigned char* player){
    unsigned char* glow_object_manager = get_glow_object_manager();
    int glow_index = player_get_glow_index(player);
    struct glow_object_t enemy_glow = {
        .entity = player,
        .r = 1.f,
        .g = 0.f,
        .b = 0.f,
        .a = 0.6f,
        .glow_alpha_max = 0.6,
        .render_occluded = 1,
        .render_unoccluded = 0,
        .glow_style = 0,
        .full_bloom = 0
    };

    int num = write(glow_object_manager + (glow_index * (sizeof(struct glow_object_t))),
                    &enemy_glow,
                    sizeof(enemy_glow));

    printf("glow_object_manager: %lx\n", glow_object_manager);
    printf("glow_index: %d\n", glow_index);
    printf("num: %d\n", num);
}

// -------------------------------------------------

#define MAX_PLAYERS (32)

int main(){
    struct position_t enemies_positions[MAX_PLAYERS];

    csgo_handle = open_csgo();
    ASSERT_TO(csgo_handle == NULL, end);

    client_base = module_get_base("\\client.dll");
    ASSERT_TO(client_base == NULL, end_close_proc);

    engine_base = module_get_base("\\engine.dll");
    ASSERT_TO(engine_base == NULL, end_close_proc);

    // start drawing context
    dc_handle = GetDC(NULL);
    ASSERT_TO(dc_handle == NULL, end_close_proc);


    while(get_local_player()){      // while we in active game
        struct location_t local_loc = player_get_location(get_local_player());

        int i;
        for (i = 1; i < MAX_PLAYERS; i++){
            unsigned char* player = get_player(i);
            if (player == NULL) continue;
            if (player == get_local_player()) continue; // ignore us.
            if (!player_is_real(player)) continue;

            // is alive?
            int player_health = player_get_health(player);
            if (player_health < 1 || player_health > 100) continue; 

            // if player in my team skip.
            if (player_get_team(player) == player_get_team(get_local_player())) continue;
            
            // glow_enenmy(player); // WIP:

            // Draw head of enemy.
            struct location_t head_loc = player_get_head_location(player);
            if (head_loc.x == -1 && head_loc.y == -1 && head_loc.z == -1) continue;

            struct position_t head_pos = game_to_screen(head_loc);
            if (head_pos.x <= 0 || head_pos.y <= 0) continue;
            if (head_pos.x >= SCREEN_WIDTH || head_pos.y >= SCREEN_HEIGHT) continue;

            float enemy_distance = distance(head_loc.x, head_loc.y, local_loc.x, local_loc.y);
            draw_enemy(head_pos, enemy_distance);
        }
    }
    
    // stop drawing context
    ReleaseDC(NULL, dc_handle);

end_close_proc:
    CloseHandle(csgo_handle);
end:
    return 0;
}
