// ImGui - standalone example application for DirectX 9
// If you are new to ImGui, see examples/README.txt and documentation at the top of imgui.cpp.

#define _CRT_SECURE_NO_WARNINGS

#define NOMINMAX

#include <imgui/imgui.h>
#include <d3d9.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <tchar.h>

#include "DLL_VERSION.H"

#include "config.h"
#include "command.h"
#include "render.h"
#include "textures.h"
#include "framerate.h"
#include "sound.h"
#include "hook.h"
#include "input.h"
#include "keyboard.h"


#include <string>
#include <vector>


#include <algorithm>
#include <Mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <atlbase.h>

IAudioMeterInformation*
TBFix_GetAudioMeterInfo (void)
{
  CComPtr <IMMDeviceEnumerator> pDevEnum = nullptr;

  if (FAILED ((pDevEnum.CoCreateInstance (__uuidof (MMDeviceEnumerator)))))
    return nullptr;

  CComPtr <IMMDevice> pDevice;
  if ( FAILED (
         pDevEnum->GetDefaultAudioEndpoint ( eRender,
                                               eConsole,
                                                 &pDevice )
              )
     ) return nullptr;

  IAudioMeterInformation* pMeterInfo = nullptr;

  HRESULT hr = pDevice->Activate ( __uuidof (IAudioMeterInformation),
                                     CLSCTX_ALL,
                                       nullptr,
                                         IID_PPV_ARGS_Helper (&pMeterInfo) );

  if (SUCCEEDED (hr))
    return pMeterInfo;

  return nullptr;
}



__declspec (dllimport)
bool
SK_ImGui_ControlPanel (void);


bool show_config          = true;
bool show_special_k_cfg   = false;
bool show_test_window     = false;
bool show_texture_mod_dlg = false;

ImVec4 clear_col = ImColor (114, 144, 154);

struct {
  std::vector <const char*> array;
  int                       sel   = 0;
} gamepads;

void
__TBF_DefaultSetOverlayState (bool)
{
}

void
TBFix_PauseGame (bool pause)
{
  typedef void (__stdcall *SK_SteamAPI_SetOverlayState_pfn)(bool active);

  static SK_SteamAPI_SetOverlayState_pfn
    SK_SteamAPI_SetOverlayState =
      (SK_SteamAPI_SetOverlayState_pfn)

    TBF_ImportFunctionFromSpecialK (
      "SK_SteamAPI_SetOverlayState",
        &__TBF_DefaultSetOverlayState
    );

  SK_SteamAPI_SetOverlayState (pause);
}

bool reset_frame_history = false;
int  cursor_refs         = 0;

typedef DWORD (*SK_ImGui_Toggle_pfn)(void);
SK_ImGui_Toggle_pfn SK_ImGui_Toggle_Original = nullptr;

extern void TBFix_ShowCursor    (void);
extern void TBFix_ReleaseCursor (void);
extern void TBFix_CaptureCursor (void);

void
TBFix_ToggleConfigUI (void)
{
  DWORD dwPos = GetMessagePos ();

  ImGuiIO& io =
    ImGui::GetIO ();

  if (! io.WantCaptureMouse)
    SendMessage (tbf::RenderFix::hWndDevice, WM_MOUSEMOVE, 0x00, dwPos);

  SK_ImGui_Toggle_Original ();

  reset_frame_history = true;

  bool* visible =
    (bool *)SK_GetCommandProcessor ()->ProcessCommandLine ("ImGui.Visible").getVariable()->getValuePointer ();

  config.input.ui.visible = *visible;

  if (config.input.ui.pause)
    TBFix_PauseGame (config.input.ui.visible);


  if (config.input.ui.visible)
  {
    TBFix_CaptureCursor ();
    TBFix_ShowCursor    ();
  } else {
    TBFix_ReleaseCursor ();
  }

  if (! io.WantCaptureMouse)
    SendMessage (tbf::RenderFix::hWndDevice, WM_MOUSEMOVE, 0x00, dwPos);

  TBF_SaveConfig ();
}

extern
bool
TBFix_TextureModDlg (void);

void
TBFix_GamepadConfigDlg (void)
{
  if (gamepads.array.size () == 0)
  {
    if (GetFileAttributesA ("TBFix_Res\\Gamepads\\") != INVALID_FILE_ATTRIBUTES)
    {
      std::vector <std::string> gamepads_;

      WIN32_FIND_DATAA fd;
      HANDLE           hFind  = INVALID_HANDLE_VALUE;
      int              files  = 0;
      LARGE_INTEGER    liSize = { 0 };

      hFind = FindFirstFileA ("TBFix_Res\\Gamepads\\*", &fd);

      if (hFind != INVALID_HANDLE_VALUE)
      {
        do {
          if ( fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY &&
               fd.cFileName[0]    != '.' )
          {
            gamepads_.push_back (fd.cFileName);
          }
        } while (FindNextFileA (hFind, &fd) != 0);

        FindClose (hFind);
      }

            char current_gamepad [128] = { '\0' };
      snprintf ( current_gamepad, 127,
                   "%ws",
                     config.input.gamepad.texture_set.c_str () );

      for (int i = 0; i < gamepads_.size (); i++)
      {
        gamepads.array.push_back (
          _strdup ( gamepads_ [i].c_str () )
        );

        if (! _stricmp (gamepads.array [i], current_gamepad))
          gamepads.sel = i;
      }
    }
  }

  if (ImGui::BeginPopupModal ("Gamepad Config", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_ShowBorders))
  {
    int orig_sel = gamepads.sel;

    if (ImGui::ListBox ("Gamepad\nIcons", &gamepads.sel, gamepads.array.data (), (int)gamepads.array.size (), 3))
    {
      if (orig_sel != gamepads.sel)
      {
        wchar_t pad [128] = { };

        swprintf_s ( pad, 128,
                       L"%hs",
                         gamepads.array [gamepads.sel]
                   );

        config.input.gamepad.texture_set    = pad;
        tbf::RenderFix::need_reset.textures = true;
      }

      ImGui::CloseCurrentPopup ();
    }

    ImGui::EndPopup();
  }
}

#define IM_ARRAYSIZE(_ARR)  ((int)(sizeof(_ARR)/sizeof(*_ARR)))

IMGUI_API
void
ImGui_ImplDX9_NewFrame (void);


bool
TBFix_DrawConfigUI (void)
{
  static bool need_restart = false;
  static bool was_reset    = false;

  ImGui_ImplDX9_NewFrame ();

  ImGuiIO& io =
    ImGui::GetIO ();

  ImGui::SetNextWindowPosCenter       (ImGuiSetCond_Always);
  ImGui::SetNextWindowSizeConstraints (ImVec2 (665, 50), ImVec2 ( ImGui::GetIO ().DisplaySize.x * 0.95f,
                                                                    ImGui::GetIO ().DisplaySize.y * 0.95f ) );

  if (was_reset) {
    ImGui::SetNextWindowSize (ImVec2 (665, 50), ImGuiSetCond_Always);
    was_reset = false;
  }

  bool show_config = true;

  ImGui::Begin ( "Tales of Berseria \"Fix\" (v " TBF_VERSION_STR_A ") Control Panel",
                   &show_config,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_ShowBorders );

  ImGui::PushItemWidth (ImGui::GetWindowWidth () * 0.666f);

  if (ImGui::CollapsingHeader ("Framerate", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_DefaultOpen))
  {
    ImGui::TreePush ("");
    int limiter =
      config.framerate.replace_limiter ? 1 : 0;

    ImGui::Combo ( "Framerate Limiter", &limiter, "Namco          (A.K.A. Stutterfest 2017)\0"
                                                    "Special K      (Precision Timing For The Win!)\0\0", 2 );

    static float values [120]  = { 0 };
    static int   values_offset =   0;

    values [values_offset] = 1000.0f * ImGui::GetIO ().DeltaTime;
    values_offset = (values_offset + 1) % IM_ARRAYSIZE (values);

    if ((bool)limiter != config.framerate.replace_limiter)
    {
      reset_frame_history              = true;
      config.framerate.replace_limiter = limiter;

      if (config.framerate.replace_limiter)
        tbf::FrameRateFix::BlipFramerate    ();
      else
        tbf::FrameRateFix::DisengageLimiter ();
    }

    if (reset_frame_history) {
      const float fZero = 0.0f;
      memset (values, *(reinterpret_cast <const DWORD *> (&fZero)), sizeof (float) * 120);

      values_offset       = 0;
      reset_frame_history = false;
      was_reset           = true;
    }

    float sum = 0.0f;

    float min = FLT_MAX;
    float max = 0.0f;

    for (float val : values) {
      sum += val;

      if (val > max)
        max = val;

      if (val < min)
        min = val;
    }

    static char szAvg [512] = { };

    sprintf_s ( szAvg,
                  512,
                    "Avg milliseconds per-frame: %6.3f  (Target: %6.3f)\n"
                    "    Extreme frametimes:      %6.3f min, %6.3f max\n\n\n\n"
                  "Variation:  %8.5f ms  ==>  %.1f FPS  +/-  %3.1f frames",
                    sum / 120.0f, tbf::FrameRateFix::GetTargetFrametime (),
                      min, max, max - min,
                        1000.0f / (sum / 120.0f), (max - min) / (1000.0f / (sum / 120.0f)) );

    ImGui::PlotLines ( "",
                         values,
                           IM_ARRAYSIZE (values),
                             values_offset,
                               szAvg,
                                 0.0f,
                                   2.0f * tbf::FrameRateFix::GetTargetFrametime (),
                                     ImVec2 (0, 80) );

    ImGui::SameLine ();

    if (! config.framerate.replace_limiter) {
      ImGui::BeginChild     ( "LimitDisclaimer", ImVec2 ( 330.0f, 80.0f ) );
      ImGui::TextColored ( ImVec4 (1.0f, 1.0f, 0.0f, 1.0f),
                             "Working limiters do not resemble seismographs!" );

      ImGui::Separator      ();

      ImGui::PushStyleColor ( ImGuiCol_Text, ImVec4 ( 0.666f, 0.666f, 0.666f, 1.0f ) );
      ImGui::TextWrapped    ( "\nOnly use this limiter for reference purposes, or when making "
                              "changes to the in-game framerate setting." );
      ImGui::PopStyleColor  ();
      ImGui::EndChild       ();
    }

    else {
      ImGui::BeginChild     ( "LimitDisclaimer", ImVec2 ( 310.0f, 80.0f ) );
      ImGui::TextColored    ( ImVec4 ( 0.2f, 1.0f, 0.2f, 1.0f),
                                "This is how a framerate limiter should work." );

      ImGui::Separator      ();

      ImGui::PushStyleColor ( ImGuiCol_Text, ImVec4 ( 0.666f, 0.666f, 0.666f, 1.0f ) );
      ImGui::TextWrapped    ( "\nSelect Namco's Limiter before making any changes to framerate limit in-game,"
                              " or the setting will not take effect." );
      ImGui::PopStyleColor  ();
      ImGui::EndChild       ();

      bool changed = ImGui::SliderFloat ( "Special K Framerate Tolerance", &config.framerate.tolerance, 0.005f, 0.5);

      if (ImGui::IsItemHovered ()) {
        ImGui::BeginTooltip ();
        ImGui::PushStyleColor (ImGuiCol_Text, ImColor (0.95f, 0.75f, 0.25f, 1.0f));
        ImGui::Text           ("Controls Framerate Smoothness\n\n");
        ImGui::PushStyleColor (ImGuiCol_Text, ImColor (0.75f, 0.75f, 0.75f, 1.0f));
        ImGui::Text           ("  Lower = Smoother, but setting ");
        ImGui::SameLine       ();
        ImGui::PushStyleColor (ImGuiCol_Text, ImColor (0.95f, 1.0f, 0.65f, 1.0f));
        ImGui::Text           ("too low");
        ImGui::SameLine       ();
        ImGui::PushStyleColor (ImGuiCol_Text, ImColor(0.75f, 0.75f, 0.75f, 1.0f));
        ImGui::Text           (" will cause framerate instability...");
        ImGui::PopStyleColor  (4);
        ImGui::EndTooltip     ();
      }

      if (changed) {
        tbf::FrameRateFix::DisengageLimiter ();
        SK_GetCommandProcessor ()->ProcessCommandFormatted ( "LimiterTolerance %f", config.framerate.tolerance );
        tbf::FrameRateFix::BlipFramerate    ();
      }

      bool* wait_for_vblank =
        (bool *)SK_GetCommandProcessor ()->ProcessCommandLine ("WaitForVBLANK").getVariable ()->getValuePointer ();

      ImGui::Checkbox ("Wait for VBLANK", wait_for_vblank);

      if (ImGui::IsItemHovered ()) {
        ImGui::BeginTooltip ();
        ImGui::PushStyleColor (ImGuiCol_Text, ImColor (0.95f, 0.75f, 0.25f, 1.0f));
        ImGui::Text           ("Lower CPU Usage and Input Latency\n\n");
        ImGui::PushStyleColor (ImGuiCol_Text, ImColor (0.75f, 0.75f, 0.75f, 1.0f));
        ImGui::BulletText     ("This aligns timing of the framerate limiter to your monitor's refresh rate -- it IS NOT VSYNC!");
        ImGui::BulletText     ("If you enable this, you will need to adjust the Limiter Tolerance slider (lower value) for best results.");
        ImGui::PopStyleColor  (2);
        ImGui::EndTooltip     ();
      }
    }

    //ImGui::Text ( "Application average %.3f ms/frame (%.1f FPS)",
                    //1000.0f / ImGui::GetIO ().Framerate,
                              //ImGui::GetIO ().Framerate );
    ImGui::TreePop ();
  }

  if (ImGui::CollapsingHeader ("Textures", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_DefaultOpen))
  {
    ImGui::TreePush ("");

    if (ImGui::CollapsingHeader ("Experimental")) {
      ImGui::TreePush ("");

      tbf::RenderFix::need_reset.graphics |=
        ImGui::Checkbox ("Enable map menu resolution fix", &config.render.fix_map_res);

      if (ImGui::IsItemHovered ())
      {
        ImGui::BeginTooltip ();
        ImGui::Text       ( "This feature is experimental and only works at 16:9 resolutions, I am not positive that it"
                            " will not break something unrelated." );
        ImGui::Separator  ();
        ImGui::Bullet     (); ImGui::SameLine ();
        ImGui::Text       ("That is where you come in, my faithful guinea pig >:)" );
        ImGui::EndTooltip ();
      }

      ImGui::Checkbox ("Clamp Non-Power-Of-Two Texture Coordinates", &config.textures.clamp_npot_coords);

      if (ImGui::IsItemHovered ())
      {
        ImGui::SetTooltip ("Fixes blurring and black line artifacts on key UI elements, but may break third-party overlays...");
      }

      ImGui::TreePop      ();
    }

    if (ImGui::CollapsingHeader ("Quality"))
    {
      ImGui::TreePush ("");

      if (ImGui::Checkbox ("Generate Mipmaps", &config.textures.remaster)) tbf::RenderFix::need_reset.graphics = true;

      if (ImGui::IsItemHovered ())
        ImGui::SetTooltip ("Eliminates distant texture aliasing and shimmering caused by missing/incomplete mipmaps.");

      if (config.textures.remaster)
      {
        ImGui::TreePush ("");

        if (ImGui::Checkbox ("Do Not Compress Generated Mipmaps", &config.textures.uncompressed)) tbf::RenderFix::need_reset.graphics = true;

        if (ImGui::IsItemHovered ())
          ImGui::SetTooltip ("Uses more VRAM, but avoids texture compression artifacts on generated mipmaps.");

        ImGui::Checkbox ("Show Loading Activity in OSD During Mipmap Generation", &config.textures.show_loading_text);
        ImGui::TreePop  ();
      }

      ImGui::SliderFloat ("Mipmap LOD Bias", &config.textures.lod_bias, -3.0f, /*config.textures.uncompressed ? 16.0f :*/ 3.0f);

      if (ImGui::IsItemHovered ())
      {
        ImGui::BeginTooltip ();
        ImGui::Text         ("Controls texture sharpness;  -3 = Sharpest (WILL shimmer),  0 = Neutral,  3 = Blurry");
        ImGui::EndTooltip   ();
      }

      ImGui::TreePop ();
    }

    if (ImGui::CollapsingHeader ("Performance", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_DefaultOpen))
    {
      extern bool __remap_textures;

      ImGui::PushStyleVar(ImGuiStyleVar_ChildWindowRounding, 15.0f);
      ImGui::TreePush  ("");
      ImGui::BeginChild  ("Texture Details", ImVec2 (0, 130), true);

      ImGui::Columns   ( 3 );
        ImGui::PushStyleColor (ImGuiCol_Text, ImVec4 (1.0f, 1.0f, 1.0f, 1.0f));
        ImGui::Text    ( "          Size" );                                                                 ImGui::NextColumn ();
        ImGui::Text    ( "      Activity" );                                                                 ImGui::NextColumn ();
        ImGui::Text    ( "       Savings" );
        ImGui::PopStyleColor  ();
      ImGui::Columns   ( 1 );

      ImGui::PushStyleColor
                       ( ImGuiCol_Text, ImVec4 (0.75f, 0.75f, 0.75f, 1.0f) );

      ImGui::Separator (   );

      ImGui::Columns   ( 3 );
        ImGui::Text    ( "%#6zu MiB Total",
                                                       tbf::RenderFix::tex_mgr.cacheSizeTotal () >> 20ULL ); ImGui::NextColumn (); 
        ImGui::TextColored
                       (ImVec4 (0.3f, 1.0f, 0.3f, 1.0f),
                         "%#5lu     Hits",             tbf::RenderFix::tex_mgr.getHitCount    ()          );  ImGui::NextColumn ();
      ImGui::Columns   ( 1 );

      ImGui::Separator (   );

      ImColor  active   ( 1.0f,  1.0f,  1.0f, 1.0f);
      ImColor  inactive (0.75f, 0.75f, 0.75f, 1.0f);
      ImColor& color   = __remap_textures ? inactive : active;

      ImGui::Columns   ( 3 );
        ImGui::TextColored ( color,
                               "%#6zu MiB Base",
                                                       tbf::RenderFix::tex_mgr.cacheSizeBasic () >> 20ULL );  ImGui::NextColumn (); 
        if (ImGui::IsItemClicked ())
          __remap_textures = false;

        ImGui::TextColored
                       (ImVec4 (1.0f, 0.3f, 0.3f, 1.0f),
                         "%#5lu   Misses",             tbf::RenderFix::tex_mgr.getMissCount   ()          );  ImGui::NextColumn ();
        ImGui::Text    ( "Time:    %#7.01lf  s  ", tbf::RenderFix::tex_mgr.getTimeSaved       () / 1000.0f);
      ImGui::Columns   ( 1 );

      ImGui::Separator (   );

      color = __remap_textures ? active : inactive;

      ImGui::Columns   ( 3 );
        ImGui::TextColored ( color,
                               "%#6zu MiB Injected",
                                                       tbf::RenderFix::tex_mgr.cacheSizeInjected () >> 20ULL ); ImGui::NextColumn (); 

        if (ImGui::IsItemClicked ())
          __remap_textures = true;

        ImGui::TextColored (ImVec4 (0.555f, 0.555f, 1.0f, 1.0f),
                         "%.2f  Hit/Miss",          (double)tbf::RenderFix::tex_mgr.getHitCount  () / 
                                                     (double)tbf::RenderFix::tex_mgr.getMissCount()          ); ImGui::NextColumn ();
        ImGui::Text    ( "Driver: %#7zu MiB  ",    tbf::RenderFix::tex_mgr.getByteSaved      () >> 20ULL );

      ImGui::PopStyleColor
                       (   );
      ImGui::Columns   ( 1 );

      ImGui::Separator (   );

      ImGui::TreePush  ("");
      ImGui::Checkbox  ("Enable Texture QuickLoad", &config.textures.quick_load);
      ImGui::TreePop   (  );
      
      if (ImGui::IsItemHovered ())
      {
        ImGui::BeginTooltip  (  );
          ImGui::TextColored ( ImVec4 (0.9f, 0.9f, 0.9f, 1.f), 
                                 "Only read the first texture level-of-detail from disk and split generation of the rest across all available CPU cores." );
          ImGui::Separator   (  );
          ImGui::TreePush    ("");
            ImGui::Bullet    (  ); ImGui::SameLine ();
            ImGui::TextColored ( ImColor (0.95f, 0.75f, 0.25f, 1.0f), 
                                   "Typically this reduces hitching and load-times, but some pop-in may "
                                   "become visible on lower-end CPUs." );
            ImGui::Bullet    (  ); ImGui::SameLine ();
            ImGui::TextColored  ( ImColor (0.95f, 0.75f, 0.25f, 1.0f),
                                   "Ideally, this should be used with texture compression disabled." );
            ImGui::SameLine  (  );
            ImGui::Text      ("[Quality/Generate Mipmaps]");
          ImGui::TreePop     (  );
        ImGui::EndTooltip    (  );
      }

      if (config.textures.quick_load) {
        ImGui::SameLine ();

        if (ImGui::SliderInt("# of Threads", &config.textures.worker_threads, 2, 10)) {
          need_restart = true;
        }

        if (ImGui::IsItemHovered ())
          ImGui::SetTooltip ("Lower is actually better, the only reason to adjust this would be if you have an absurd number of CPU cores and pop-in bothers you ;)");
      }

      ImGui::EndChild    ( );
      ImGui::PopStyleVar ( );
      ImGui::TreePop     ( );
    }

    if (ImGui::Button ("Texture Modding Tools")) {
      show_texture_mod_dlg = (! show_texture_mod_dlg);
    }

    ImGui::TreePop ();
  }

#if 0
  if (ImGui::CollapsingHeader ("Shader Options"))
  {It
    ImGui::Checkbox ("Dump Shaders", &config.render.dump_shaders);
  }
#endif

  if (ImGui::CollapsingHeader ("Shadows"))
  {
    ImGui::TreePush ("");
    struct shadow_imp_s
    {
      shadow_imp_s (int scale)
      {
        scale = std::abs (scale);

        if (scale > 3)
          scale = 3;

        radio    = scale;
        last_sel = radio;
      }

      int radio    = 0;
      int last_sel = 0;
    };

    static shadow_imp_s shadows     (config.render.shadow_rescale);
    static shadow_imp_s env_shadows (config.render.env_shadow_rescale);

    ImGui::Combo      ("Character Shadow Resolution",     &shadows.radio,     "Normal\0Enhanced\0High\0Ultra\0\0");
    ImGui::Combo      ("Environmental Shadow Resolution", &env_shadows.radio, "Normal\0High\0Ultra\0\0");

    ImGui::PushStyleColor (ImGuiCol_Text, ImColor (0.85f, 0.25f, 0.855f, 1.0f));
    ImGui::Bullet         (); ImGui::SameLine ();
    ImGui::TextWrapped    ("Changes to these settings will produce weird results until you change Screen Resolution in-game..." );
    ImGui::PopStyleColor  ();

    if (env_shadows.radio != env_shadows.last_sel) {
      config.render.env_shadow_rescale    = env_shadows.radio;
      env_shadows.last_sel                = env_shadows.radio;
      tbf::RenderFix::need_reset.graphics = true;
    }

    if (shadows.radio != shadows.last_sel) {
      config.render.shadow_rescale        = -shadows.radio;
      shadows.last_sel                    =  shadows.radio;
      tbf::RenderFix::need_reset.graphics = true;
    }

    ImGui::TreePop ();
  }

  if (ImGui::CollapsingHeader ("Input"))
  {
    ImGui::TreePush  ("");

    if (ImGui::CollapsingHeader ("Keyboard"))
      tbf::KeyboardFix::DrawControlPanel ();

    if (ImGui::CollapsingHeader ("Gamepad / AI"))
    {
      ImGui::PushItemWidth (300);

      if (ImGui::SliderInt ("Number of Virtual Controllers (AI Fix)", &config.input.gamepad.virtual_controllers, 0, 3)) {
        extern void
        TBF_InitSDLOverride (void);
      
        TBF_InitSDLOverride ();    
      }
        
      if (ImGui::IsItemHovered ())
        ImGui::SetTooltip ("Map Players 2-4 to individual TBFix Dummy Controllers under Controller Settings, then set Control Mode = Auto for Players 2-4.");
      
      ImGui::PopItemWidth ();
    }

    ImGui::TreePop   (  );
  }

  if (ImGui::CollapsingHeader ("Audio"))
  { 
    ImGui::TreePush ("");

    if (ImGui::CollapsingHeader ("Volume Levels"))
    {
      IAudioMeterInformation* pMeterInfo =
        TBFix_GetAudioMeterInfo ();

      if (pMeterInfo != nullptr)
      {
        UINT channels = 0;

        if (SUCCEEDED (pMeterInfo->GetMeteringChannelCount (&channels)))
        {
          static float channel_peaks_    [32];

          if (channels < 4)
          {
            ImGui::TextColored ( ImVec4 (0.9f, 0.7f, 0.2f, 1.0f),
                                   "WARNING: Do not select Surround in-game, you will be missing center channel audio on your hardware!" );
            ImGui::Separator   ();
          }

          struct
          {
            struct {
              float inst_min = FLT_MAX;  DWORD dwMinSample = 0;  float disp_min = FLT_MAX;
              float inst_max = FLT_MIN;  DWORD dwMaxSample = 0;  float disp_max = FLT_MIN;
            } vu_peaks;

            float peaks [120];
            int   current_idx;
          } static history [32];

          #define VUMETER_TIME 300

          ImGui::Columns (2);

          for (int i = 0 ; i < std::min (config.audio.channels, channels); i++)
          {
            if (SUCCEEDED (pMeterInfo->GetChannelsPeakValues (channels, channel_peaks_)))
            {
              history [i].vu_peaks.inst_min = std::min (history [i].vu_peaks.inst_min, channel_peaks_ [i]);
              history [i].vu_peaks.inst_max = std::max (history [i].vu_peaks.inst_max, channel_peaks_ [i]);

              history [i].vu_peaks.disp_min    = history [i].vu_peaks.inst_min;

              if (history [i].vu_peaks.dwMinSample < timeGetTime () - VUMETER_TIME * 3) {
                history [i].vu_peaks.inst_min    = channel_peaks_ [i];
                history [i].vu_peaks.dwMinSample = timeGetTime ();
              }

              history [i].vu_peaks.disp_max    = history [i].vu_peaks.inst_max;

              if (history [i].vu_peaks.dwMaxSample < timeGetTime () - VUMETER_TIME * 3) {
                history [i].vu_peaks.inst_max    = channel_peaks_ [i];
                history [i].vu_peaks.dwMaxSample = timeGetTime ();
              }

              history [i].peaks [history [i].current_idx] = channel_peaks_ [i];
              history [i].current_idx = (history [i].current_idx + 1) % IM_ARRAYSIZE (history [i].peaks);

              ImGui::BeginGroup ();

              ImGui::PlotLines ( "",
                                  history [i].peaks,
                                    IM_ARRAYSIZE (history [i].peaks),
                                      history [i].current_idx,
                                        "",
                                             history [i].vu_peaks.disp_min,
                                             1.0f,
                                              ImVec2 (ImGui::GetContentRegionAvailWidth (), 80) );

              //char szName [64];
              //sprintf (szName, "Channel: %lu", i);

              ImGui::PushStyleColor (ImGuiCol_PlotHistogram,     ImVec4 (0.9f, 0.1f, 0.1f, 0.15f));
              ImGui::ProgressBar    (history [i].vu_peaks.disp_max, ImVec2 (-1.0f, 0.0f));
              ImGui::PopStyleColor  ();

              ImGui::ProgressBar    (channel_peaks_ [i],          ImVec2 (-1.0f, 0.0f));

              ImGui::PushStyleColor (ImGuiCol_PlotHistogram,     ImVec4 (0.1f, 0.1f, 0.9f, 0.15f));
              ImGui::ProgressBar    (history [i].vu_peaks.disp_min, ImVec2 (-1.0f, 0.0f));
              ImGui::PopStyleColor  ();
              ImGui::EndGroup ();

              if (! (i % 2))
              {
                ImGui::SameLine (); ImGui::NextColumn ();
              } else {
                ImGui::Columns   ( 1 );
                ImGui::Separator (   );
                ImGui::Columns   ( 2 );
              }
            }
          }

          ImGui::Columns (1);
        }

        pMeterInfo->Release ();
      }
    }

    if (ImGui::CollapsingHeader ("Mix Format"))
    {
      if (tbf::SoundFix::wasapi_init)
      {
        ImGui::PushStyleVar (ImGuiStyleVar_ChildWindowRounding, 16.0f);
        ImGui::BeginChild  ("Audio Details", ImVec2 (0, 80), true);

          ImGui::Columns   (3);
          ImGui::Text      ("");                                                                     ImGui::NextColumn ();
          ImGui::Text      ("Sample Rate");                                                          ImGui::NextColumn ();
          ImGui::Text      ("Channel Setup");
          ImGui::Columns   (1);
          ImGui::Separator ();
          ImGui::Columns   (3);
          ImGui::Text      ( "Game (SoundCore)");                                                    ImGui::NextColumn ();
          ImGui::Text      ( "%.2f kHz @ %lu-bit", (float)tbf::SoundFix::snd_core_fmt.nSamplesPerSec / 1000.0f,
                                                     tbf::SoundFix::snd_core_fmt.wBitsPerSample );   ImGui::NextColumn ();
          ImGui::Text      ( "%lu",                  tbf::SoundFix::snd_core_fmt.nChannels );
          ImGui::Columns   (1);
          ImGui::Separator ();
          ImGui::Columns   (3);
          ImGui::Text      ( "Device");                                                              ImGui::NextColumn ();
          ImGui::Text      ( "%.2f kHz @ %lu-bit", (float)tbf::SoundFix::snd_device_fmt.nSamplesPerSec / 1000.0f,
                                                     tbf::SoundFix::snd_device_fmt.wBitsPerSample ); ImGui::NextColumn ();
          ImGui::Text      ( "%lu",                  tbf::SoundFix::snd_device_fmt.nChannels );
          ImGui::Columns   (1);

        ImGui::EndChild    ();
        ImGui::PopStyleVar ();
      }

      ImGui::Separator ();

      need_restart |= ImGui::Checkbox ("Enable Override", &config.audio.enable_fix);

      if (config.audio.enable_fix)
      {
        IAudioMeterInformation* pMeterInfo =
          TBFix_GetAudioMeterInfo ();

        UINT channels = 2;

        if (pMeterInfo != nullptr)
        {
          if (FAILED (pMeterInfo->GetMeteringChannelCount (&channels)))
            channels = 2;

          pMeterInfo->Release ();
        }

        ImGui::TreePush ("");
          need_restart |= ImGui::RadioButton ("Stereo",       (int *)&config.audio.channels, 2);

          if (channels >= 4)
          {
            ImGui::SameLine();  need_restart |= ImGui::RadioButton ("Quadraphonic",   (int *)&config.audio.channels, 4);

            if (channels >= 6) {
              ImGui::SameLine (); need_restart |= ImGui::RadioButton ("5.1 Surround", (int *)&config.audio.channels, 6);
            }
          }
        ImGui::TreePop  (  );
        
        ImGui::TreePush ("");
        int sel;
        
             if (config.audio.sample_hz == 48000) sel = 2;
        else if (config.audio.sample_hz == 44100) sel = 1;
        else                                      sel = 0;
        
        need_restart |= ImGui::Combo ("Sample Rate", &sel, " Unlimited (expect distortion)\0 44.1 kHz\0 48.0 kHz\0\0", 3);
        
             if (sel == 0) config.audio.sample_hz = -1;
        else if (sel == 1) config.audio.sample_hz = 44100;
        else               config.audio.sample_hz = 48000;
           
        
        need_restart |= ImGui::Checkbox ("Use Compatibility Mode", &config.audio.compatibility);
        
        if (ImGui::IsItemHovered ())
          ImGui::SetTooltip ("May reduce audio quality, but can help with some weird USB headsets and Windows 7 / Older.");
        ImGui::TreePop ();
      }
    }
    ImGui::TreePop ();
  }

  ImGui::PopItemWidth ();

  ImGui::Separator (   );
  ImGui::Columns   ( 2 );

  if (ImGui::Button ("   Gamepad Config   "))
    ImGui::OpenPopup ("Gamepad Config");

  TBFix_GamepadConfigDlg ();

  ImGui::SameLine      ();

  if (ImGui::Button ("   Special K Config   "))
    show_special_k_cfg = (! show_special_k_cfg);

  ImGui::SameLine (0.0f, 60.0f);

  if (ImGui::Selectable ("...", show_test_window))
    show_test_window = (! show_test_window);

  ImGui::NextColumn ();

  if ( ImGui::Checkbox ("Pause Game While This Menu Is Open", &config.input.ui.pause) )
    TBFix_PauseGame (config.input.ui.pause);

  bool extra_details = false;

  if (need_restart || tbf::RenderFix::need_reset.graphics || tbf::RenderFix::need_reset.textures)
    extra_details = true;

  if (extra_details)
  {
    ImGui::Columns    ( 1 );
    ImGui::Separator  (   );

    if (need_restart)
    {
      ImGui::PushStyleColor (ImGuiCol_Text, ImVec4 (1.0f, 0.4f, 0.15f, 1.0f));
      ImGui::BulletText     ("Game Restart Required");
      ImGui::PopStyleColor  ();
    }
    
    if (tbf::RenderFix::need_reset.graphics || tbf::RenderFix::need_reset.textures)
    {
      ImGui::PushStyleColor  (ImGuiCol_Text, ImVec4 (1.0f, 0.8f, 0.2f, 1.0f));
      ImGui::Bullet          ( ); ImGui::SameLine ();
      ImGui::TextWrapped     ( "You have made changes that will not apply until you change Screen Modes in Graphics Settings, "
                              "or by performing Alt + Tab with the game set to Fullscreen mode.\n" );
      ImGui::PopStyleColor   ( );
      ImGui::PopTextWrapPos  ( );
    }
  }
  
  ImGui::End ();


  if (show_test_window)
  {
    ImGui::SetNextWindowPos (ImVec2 (650, 20), ImGuiSetCond_FirstUseEver);
    ImGui::ShowTestWindow   (&show_test_window);
  }

  if (show_special_k_cfg)  show_special_k_cfg = SK_ImGui_ControlPanel ();

  if (show_texture_mod_dlg)
  {
    show_texture_mod_dlg = TBFix_TextureModDlg ();
  }

  if ( SUCCEEDED (
         tbf::RenderFix::pDevice->BeginScene ()
       )
     )
  {
    ImGui::Render                     ();
    tbf::RenderFix::pDevice->EndScene ();
  }

  if (! show_config)
  {
    TBFix_ToggleConfigUI ();
  }

  return show_config;
}


typedef DWORD (*SK_ImGui_DrawFrame_pfn)(DWORD dwFlags, void* user);
SK_ImGui_DrawFrame_pfn SK_ImGui_DrawFrame_Original = nullptr;

DWORD
TBFix_ImGui_DrawFrame (DWORD dwFlags, void* user)
{
  TBFix_DrawConfigUI ();

  return 1;
}


void
TBFix_ImGui_Init (void)
{
  TBF_CreateDLLHook ( config.system.injector.c_str (),
                        "SK_ImGui_Toggle",
                        TBFix_ToggleConfigUI,
             (LPVOID *)&SK_ImGui_Toggle_Original );

  TBF_CreateDLLHook ( config.system.injector.c_str (),
                        "SK_ImGui_DrawFrame",
                     TBFix_ImGui_DrawFrame,
             (LPVOID *)&SK_ImGui_DrawFrame_Original );
}


#define ID_TIMER  1
#define TIMER_PERIOD  125