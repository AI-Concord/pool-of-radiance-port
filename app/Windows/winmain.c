// Pool of Radiance for Windows 11: the Win32 / Direct3D 12 front end, the counterpart of app/App/main.swift.
// The game itself is the same C (port/ with the platform layer of re/emu/emu.c, built without the 8086 interpreter);
// this file only shows its 320x200 frames, plays its PC speaker, passes keys in, and records sessions for bug reports.
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../Engine/engine.h"
#include "resource.h"

#define W ENGINE_WIDTH
#define H ENGINE_HEIGHT
static HWND hwnd;
static int fullscreen; static WINDOWPLACEMENT saved_place = { sizeof saved_place };
static char game_dir[MAX_PATH], save_dir[MAX_PATH], rec_root[MAX_PATH], session[MAX_PATH];
static unsigned char frame[W * H * 4];

static int sharp_pixels;                       // View menu (registry SharpPixels)
// settings, in HKEY_CURRENT_USER\Software\Pool of Radiance
static int reg_get(const char *name, DWORD dflt) { DWORD v = dflt, n = sizeof v; RegGetValueA(HKEY_CURRENT_USER, "Software\\Pool of Radiance", name, RRF_RT_REG_DWORD, NULL, &v, &n); return (int)v; }
static void reg_set(const char *name, DWORD v) {
    HKEY k; if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Pool of Radiance", 0, NULL, 0, KEY_WRITE, NULL, &k, NULL) == ERROR_SUCCESS) { RegSetValueExA(k, name, 0, REG_DWORD, (BYTE *)&v, sizeof v); RegCloseKey(k); }
}

// ---------------------------------------------------------------- Direct3D 12
static ID3D12Device *dev; static ID3D12CommandQueue *queue; static IDXGISwapChain3 *swap;
static ID3D12DescriptorHeap *rtv_heap, *srv_heap; static UINT rtv_size;
static ID3D12Resource *backbuf[2], *tex, *upload;
static ID3D12CommandAllocator *alloc; static ID3D12GraphicsCommandList *list;
static ID3D12RootSignature *rootsig; static ID3D12PipelineState *pso;
static ID3D12Fence *fence; static UINT64 fence_val; static HANDLE fence_event;
static unsigned char *upload_ptr;

// Two looks, as on the Mac (see the Metal shader in app/App/main.swift for the description): Sharp, "sharp bilinear";
// Smooth (the default), xBR-style edge smoothing with four samples per screen pixel.
static const char shader_src[] =
    "cbuffer C : register(b0) { float2 scale; float2 ppt; float mode; float3 pad; };\n"
    "Texture2D tex : register(t0); SamplerState smp : register(s0);\n"
    "struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "V vmain(uint id : SV_VertexID) {\n"
    "  float2 p[4] = { float2(-1,-1), float2(-1,1), float2(1,-1), float2(1,1) };\n"
    "  V o; o.pos = float4(p[id] * scale, 0, 1); o.uv = float2((p[id].x + 1) * 0.5, (1 - p[id].y) * 0.5); return o; }\n"
    "float3 tx(int x, int y) { return tex.Load(int3(clamp(x, 0, 319), clamp(y, 0, 199), 0)).rgb; }\n"
    "float cdist(float3 a, float3 b) { float3 d = a - b;\n"
    "  return 48 * abs(dot(d, float3(0.299, 0.587, 0.114))) + 7 * abs(dot(d, float3(-0.169, -0.331, 0.5))) + 6 * abs(dot(d, float3(0.5, -0.419, -0.081))); }\n"
    "bool same(float3 a, float3 b) { return cdist(a, b) < 0.1; }\n"
    "float3 xbr(float2 s, float d) {\n"
    "  int ix = (int)floor(s.x), iy = (int)floor(s.y); float fx = s.x - ix, fy = s.y - iy;\n"
    "  int dx = fx < 0.5 ? -1 : 1, dy = fy < 0.5 ? -1 : 1;\n"
    "  if (dx < 0) fx = 1 - fx; if (dy < 0) fy = 1 - fy;\n"
    "  float3 E = tx(ix, iy), F = tx(ix + dx, iy), H = tx(ix, iy + dy), I = tx(ix + dx, iy + dy);\n"
    "  float3 B = tx(ix, iy - dy), D = tx(ix - dx, iy), C = tx(ix + dx, iy - dy), G = tx(ix - dx, iy + dy);\n"
    "  float3 F4 = tx(ix + 2 * dx, iy), I4 = tx(ix + 2 * dx, iy + dy), H5 = tx(ix, iy + 2 * dy), I5 = tx(ix + dx, iy + 2 * dy);\n"
    "  float e = cdist(E, C) + cdist(E, G) + cdist(I, F4) + cdist(I, H5) + 4 * cdist(H, F);\n"
    "  float i = cdist(H, D) + cdist(H, I5) + cdist(F, I4) + cdist(F, B) + 4 * cdist(E, I);\n"
    "  bool edge = e < i && !same(E, F) && !same(E, H) && (!(same(F, B) && same(F, C)) || !(same(H, D) && same(H, G)));\n"
    "  if (!edge) return E;\n"
    "  bool shallow = 2 * cdist(F, G) <= cdist(H, C) && !same(E, G) && !same(D, G);\n"
    "  bool steep = 2 * cdist(H, C) <= cdist(F, G) && !same(E, C) && !same(B, C);\n"
    "  float a = smoothstep(1.5 - d, 1.5 + d, fx + fy);\n"
    "  if (shallow) a = max(a, smoothstep(2.0 - 1.5 * d, 2.0 + 1.5 * d, fx + 2 * fy));\n"
    "  if (steep) a = max(a, smoothstep(2.0 - 1.5 * d, 2.0 + 1.5 * d, 2 * fx + fy));\n"
    "  return lerp(E, cdist(E, F) <= cdist(E, H) ? F : H, a); }\n"
    "float4 pmain(V i) : SV_Target {\n"
    "  float2 size = float2(320, 200); float2 t = i.uv * size; float2 k = max(ppt, float2(1, 1));\n"
    "  if (mode > 0.5) { float2 o = 0.25 / k; float d = 0.5 / min(k.x, k.y);\n"
    "    float3 c = xbr(t + float2(-o.x, -o.y), d) + xbr(t + float2(o.x, -o.y), d) + xbr(t + float2(-o.x, o.y), d) + xbr(t + float2(o.x, o.y), d);\n"
    "    return float4(c * 0.25, 1); }\n"
    "  float2 f0 = floor(t); float2 f = saturate((t - f0 - 0.5) * k + 0.5);\n"
    "  return float4(tex.Sample(smp, (f0 + f) / size).rgb, 1); }\n";

// The earlier shader (sharp pixels only), used if the one above does not compile on this PC.
static const char shader_fallback[] =
    "cbuffer C : register(b0) { float2 scale; float2 ppt; float mode; float3 pad; };\n"
    "Texture2D tex : register(t0); SamplerState smp : register(s0);\n"
    "struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "V vmain(uint id : SV_VertexID) {\n"
    "  float2 p[4] = { float2(-1,-1), float2(-1,1), float2(1,-1), float2(1,1) };\n"
    "  V o; o.pos = float4(p[id] * scale, 0, 1); o.uv = float2((p[id].x + 1) * 0.5, (1 - p[id].y) * 0.5); return o; }\n"
    "float4 pmain(V i) : SV_Target {\n"
    "  float2 size = float2(320, 200); float2 t = i.uv * size; float2 f0 = floor(t); float2 f = t - f0;\n"
    "  float2 k = max(ppt, float2(1, 1)); f = saturate((f - 0.5) * k + 0.5);\n"
    "  return float4(tex.Sample(smp, (f0 + f) / size).rgb, 1); }\n";
static int smooth_ok = 1;

static void fatal(const char *msg) { MessageBoxA(hwnd, msg, "Pool of Radiance cannot start", MB_ICONERROR); ExitProcess(1); }
static void gpu_wait(void) {
    ID3D12CommandQueue_Signal(queue, fence, ++fence_val);
    if (ID3D12Fence_GetCompletedValue(fence) < fence_val) { ID3D12Fence_SetEventOnCompletion(fence, fence_val, fence_event); WaitForSingleObject(fence_event, INFINITE); }
}
static void make_rtvs(void) {
    D3D12_CPU_DESCRIPTOR_HANDLE h; rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(rtv_heap, &h);
    for (int i = 0; i < 2; i++) {
        IDXGISwapChain3_GetBuffer(swap, i, &IID_ID3D12Resource, (void **)&backbuf[i]);
        ID3D12Device_CreateRenderTargetView(dev, backbuf[i], NULL, h); h.ptr += rtv_size;
    }
}
static void d3d_init(void) {
    IDXGIFactory4 *fac;
    if (FAILED(CreateDXGIFactory2(0, &IID_IDXGIFactory4, (void **)&fac))) fatal("DXGI is not available.");
    if (FAILED(D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&dev))) {
        IDXGIAdapter *warp;                                         // no Direct3D 12 hardware: Microsoft's software renderer
        if (FAILED(IDXGIFactory4_EnumWarpAdapter(fac, &IID_IDXGIAdapter, (void **)&warp)) ||
            FAILED(D3D12CreateDevice((IUnknown *)warp, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&dev))) fatal("Direct3D 12 is not available on this PC.");
    }
    D3D12_COMMAND_QUEUE_DESC qd = { D3D12_COMMAND_LIST_TYPE_DIRECT };
    ID3D12Device_CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue, (void **)&queue);
    DXGI_SWAP_CHAIN_DESC1 sd = {0};
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; sd.SampleDesc.Count = 1; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain1 *sc1;
    if (FAILED(IDXGIFactory4_CreateSwapChainForHwnd(fac, (IUnknown *)queue, hwnd, &sd, NULL, NULL, &sc1))) fatal("Could not create the swap chain.");
    IDXGISwapChain1_QueryInterface(sc1, &IID_IDXGISwapChain3, (void **)&swap); IDXGISwapChain1_Release(sc1);
    IDXGIFactory4_MakeWindowAssociation(fac, hwnd, DXGI_MWA_NO_ALT_ENTER);
    IDXGIFactory4_Release(fac);

    D3D12_DESCRIPTOR_HEAP_DESC hd = { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2 };
    ID3D12Device_CreateDescriptorHeap(dev, &hd, &IID_ID3D12DescriptorHeap, (void **)&rtv_heap);
    rtv_size = ID3D12Device_GetDescriptorHandleIncrementSize(dev, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    make_rtvs();
    D3D12_DESCRIPTOR_HEAP_DESC sh = { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE };
    ID3D12Device_CreateDescriptorHeap(dev, &sh, &IID_ID3D12DescriptorHeap, (void **)&srv_heap);

    // the 320x200 picture, and an upload buffer for it (1280-byte rows are already 256-aligned)
    D3D12_HEAP_PROPERTIES def = { D3D12_HEAP_TYPE_DEFAULT }, up = { D3D12_HEAP_TYPE_UPLOAD };
    D3D12_RESOURCE_DESC td = { D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, W, H, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE };
    ID3D12Device_CreateCommittedResource(dev, &def, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, NULL, &IID_ID3D12Resource, (void **)&tex);
    D3D12_RESOURCE_DESC bd = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, W * 4 * H, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
    ID3D12Device_CreateCommittedResource(dev, &up, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void **)&upload);
    D3D12_RANGE none = {0, 0}; ID3D12Resource_Map(upload, 0, &none, (void **)&upload_ptr);
    D3D12_SHADER_RESOURCE_VIEW_DESC vd = { DXGI_FORMAT_B8G8R8A8_UNORM, D3D12_SRV_DIMENSION_TEXTURE2D, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING };
    vd.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE sh0; srv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(srv_heap, &sh0);
    ID3D12Device_CreateShaderResourceView(dev, tex, &vd, sh0);

    // root signature: four constants (scale, pixels per texel), the picture, a linear clamped sampler
    D3D12_DESCRIPTOR_RANGE range = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0 };
    D3D12_ROOT_PARAMETER rp[2] = {0};
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; rp[0].Constants.Num32BitValues = 8; rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rp[1].DescriptorTable.NumDescriptorRanges = 1; rp[1].DescriptorTable.pDescriptorRanges = &range; rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC ss = { D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 0, 1, D3D12_COMPARISON_FUNC_NEVER, D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK, 0, D3D12_FLOAT32_MAX, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL };
    D3D12_ROOT_SIGNATURE_DESC rd = { 2, rp, 1, &ss, D3D12_ROOT_SIGNATURE_FLAG_NONE };
    ID3DBlob *blob, *err;
    if (FAILED(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err))) fatal("Could not build the root signature.");
    ID3D12Device_CreateRootSignature(dev, 0, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature, (void **)&rootsig);
    ID3D10Blob_Release(blob);

    ID3DBlob *vs, *ps;
    if (FAILED(D3DCompile(shader_src, sizeof shader_src - 1, "poolrad", NULL, NULL, "vmain", "vs_5_0", 0, 0, &vs, &err)) ||
        FAILED(D3DCompile(shader_src, sizeof shader_src - 1, "poolrad", NULL, NULL, "pmain", "ps_5_0", 0, 0, &ps, &err))) {
        char p[MAX_PATH], exe[MAX_PATH]; GetModuleFileNameA(NULL, exe, sizeof exe); *strrchr(exe, '\\') = 0;
        snprintf(p, sizeof p, "%s\\shader-error.txt", exe);                 // for a bug report
        FILE *f = fopen(p, "wb"); if (f) { if (err) fwrite(ID3D10Blob_GetBufferPointer(err), 1, ID3D10Blob_GetBufferSize(err), f); fclose(f); }
        smooth_ok = 0;
        if (FAILED(D3DCompile(shader_fallback, sizeof shader_fallback - 1, "poolrad", NULL, NULL, "vmain", "vs_5_0", 0, 0, &vs, &err)) ||
            FAILED(D3DCompile(shader_fallback, sizeof shader_fallback - 1, "poolrad", NULL, NULL, "pmain", "ps_5_0", 0, 0, &ps, &err))) fatal("Could not compile the shaders.");
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {0};
    pd.pRootSignature = rootsig;
    pd.VS.pShaderBytecode = ID3D10Blob_GetBufferPointer(vs); pd.VS.BytecodeLength = ID3D10Blob_GetBufferSize(vs);
    pd.PS.pShaderBytecode = ID3D10Blob_GetBufferPointer(ps); pd.PS.BytecodeLength = ID3D10Blob_GetBufferSize(ps);
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.SampleMask = 0xffffffff;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; pd.RasterizerState.DepthClipEnable = TRUE;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1; pd.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM; pd.SampleDesc.Count = 1;
    if (FAILED(ID3D12Device_CreateGraphicsPipelineState(dev, &pd, &IID_ID3D12PipelineState, (void **)&pso))) fatal("Could not create the pipeline.");
    ID3D10Blob_Release(vs); ID3D10Blob_Release(ps);

    ID3D12Device_CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&alloc);
    ID3D12Device_CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, NULL, &IID_ID3D12GraphicsCommandList, (void **)&list);
    ID3D12GraphicsCommandList_Close(list);
    ID3D12Device_CreateFence(dev, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence);
    fence_event = CreateEventA(NULL, FALSE, FALSE, NULL);
}
static void d3d_resize(void) {
    if (!swap) return;
    gpu_wait();
    for (int i = 0; i < 2; i++) if (backbuf[i]) { ID3D12Resource_Release(backbuf[i]); backbuf[i] = NULL; }
    IDXGISwapChain3_ResizeBuffers(swap, 2, 0, 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
    make_rtvs();
}
static void barrier(ID3D12Resource *r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b = {0};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = r; b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from; b.Transition.StateAfter = to;
    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &b);
}
static void render(void) {
    RECT rc; GetClientRect(hwnd, &rc);
    float cw = (float)(rc.right - rc.left), ch = (float)(rc.bottom - rc.top);
    if (cw < 1 || ch < 1) return;
    engine_copy_frame(frame);
    memcpy(upload_ptr, frame, sizeof frame);
    ID3D12CommandAllocator_Reset(alloc);
    ID3D12GraphicsCommandList_Reset(list, alloc, pso);
    // upload the frame into the texture
    barrier(tex, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION dst = { tex, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX }; dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION src = { upload, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT };
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_B8G8R8A8_UNORM; src.PlacedFootprint.Footprint.Width = W;
    src.PlacedFootprint.Footprint.Height = H; src.PlacedFootprint.Footprint.Depth = 1; src.PlacedFootprint.Footprint.RowPitch = W * 4;
    ID3D12GraphicsCommandList_CopyTextureRegion(list, &dst, 0, 0, 0, &src, NULL);
    barrier(tex, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    // draw it, 4:3 in the middle of the window
    UINT bi = IDXGISwapChain3_GetCurrentBackBufferIndex(swap);
    barrier(backbuf[bi], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv; rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(rtv_heap, &rtv); rtv.ptr += bi * rtv_size;
    static const float black[4] = {0, 0, 0, 1};
    ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtv, black, 0, NULL);
    ID3D12GraphicsCommandList_OMSetRenderTargets(list, 1, &rtv, FALSE, NULL);
    D3D12_VIEWPORT vp = { 0, 0, cw, ch, 0, 1 }; D3D12_RECT sc = { 0, 0, (LONG)cw, (LONG)ch };
    ID3D12GraphicsCommandList_RSSetViewports(list, 1, &vp); ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &sc);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, rootsig);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, &srv_heap);
    float aspect = cw / ch, k[8] = {1, 1, 0, 0, sharp_pixels ? 0.f : 1.f, 0, 0, 0};
    if (aspect > 4.0f / 3.0f) k[0] = (4.0f / 3.0f) / aspect; else k[1] = aspect / (4.0f / 3.0f);
    k[2] = cw * k[0] / W; k[3] = ch * k[1] / H;
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(list, 0, 8, k, 0);
    D3D12_GPU_DESCRIPTOR_HANDLE g; srv_heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(srv_heap, &g);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(list, 1, g);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D12GraphicsCommandList_DrawInstanced(list, 4, 1, 0, 0);
    barrier(backbuf[bi], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    ID3D12GraphicsCommandList_Close(list);
    ID3D12CommandList *lists[] = { (ID3D12CommandList *)list };
    ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
    IDXGISwapChain3_Present(swap, 1, 0);
    gpu_wait();
}

// ---------------------------------------------------------------- sound: the PC speaker through waveOut
static HWAVEOUT wave; static WAVEHDR hdr[4]; static float abuf[4][1024]; static HANDLE audio_event;
static DWORD WINAPI audio_thread(LPVOID p) {
    (void)p;
    for (;;) {
        for (int i = 0; i < 4; i++) if (hdr[i].dwFlags & WHDR_DONE || !(hdr[i].dwFlags & WHDR_PREPARED)) {
            if (hdr[i].dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(wave, &hdr[i], sizeof hdr[i]);
            engine_audio_render(abuf[i], 1024, 44100.0);
            hdr[i].lpData = (LPSTR)abuf[i]; hdr[i].dwBufferLength = sizeof abuf[i]; hdr[i].dwFlags = 0;
            waveOutPrepareHeader(wave, &hdr[i], sizeof hdr[i]); waveOutWrite(wave, &hdr[i], sizeof hdr[i]);
        }
        WaitForSingleObject(audio_event, 50);
    }
    return 0;
}
static void audio_init(void) {
    WAVEFORMATEX f = { WAVE_FORMAT_IEEE_FLOAT, 1, 44100, 44100 * 4, 4, 32, 0 };
    audio_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (waveOutOpen(&wave, WAVE_MAPPER, &f, (DWORD_PTR)audio_event, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) return;   // no sound device: play silently
    CreateThread(NULL, 0, audio_thread, NULL, 0, NULL);
}

// ---------------------------------------------------------------- keyboard: Windows keys to BIOS key codes
static unsigned short scan_of(char c) {
    static const char *rows[] = { "1234567890-=", "qwertyuiop[]", "asdfghjkl;'`", "\\zxcvbnm,./" };
    static const unsigned short start[] = { 0x02, 0x10, 0x1e, 0x2b };
    if (c == ' ') return 0x39;
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    for (int r = 0; r < 4; r++) { const char *p = strchr(rows[r], c); if (p && c) return (unsigned short)(start[r] + (p - rows[r])); }
    return 0;
}
static int special_key(WPARAM vk) {            // keys without a character: arrows, the editing block, F1-F10
    switch (vk) {
    case VK_UP: return 0x4800; case VK_DOWN: return 0x5000; case VK_LEFT: return 0x4b00; case VK_RIGHT: return 0x4d00;
    case VK_HOME: return 0x4700; case VK_END: return 0x4f00; case VK_PRIOR: return 0x4900; case VK_NEXT: return 0x5100;
    case VK_DELETE: return 0x5300; case VK_INSERT: return 0x5200;
    }
    if (vk >= VK_F1 && vk <= VK_F10) return (int)((0x3b + (vk - VK_F1)) << 8);
    return 0;
}
static void on_char(WPARAM c) {
    switch (c) {
    case 13: engine_post_key(0x1c0d); return; case 27: engine_post_key(0x011b); return;
    case 9: engine_post_key(0x0f09); return; case 8: engine_post_key(0x0e08); return;
    }
    if (c >= 1 && c <= 26) { engine_post_key((unsigned short)(scan_of((char)('a' + c - 1)) << 8 | c)); return; }   // Ctrl+letter
    if (c >= 32 && c < 127) engine_post_key((unsigned short)(scan_of((char)c) << 8 | c));
}

// ---------------------------------------------------------------- session recordings (see docs: Session recordings)
static int mark_count, notes_seen;
static int rec_enabled(void) {
    DWORD v = 1, n = sizeof v;
    RegGetValueA(HKEY_CURRENT_USER, "Software\\Pool of Radiance", "RecordSessions", RRF_RT_REG_DWORD, NULL, &v, &n);
    return v != 0;
}
static void rec_set(int on) {
    HKEY k; DWORD v = (DWORD)on;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Pool of Radiance", 0, NULL, 0, KEY_WRITE, NULL, &k, NULL) == ERROR_SUCCESS) { RegSetValueExA(k, "RecordSessions", 0, REG_DWORD, (BYTE *)&v, sizeof v); RegCloseKey(k); }
}
static void stamp(char *b, size_t n, const char *fmt) { time_t t = time(NULL); strftime(b, n, fmt, localtime(&t)); }
static void copy_files(const char *from, const char *to) {
    char pat[MAX_PATH], a[MAX_PATH], b[MAX_PATH]; WIN32_FIND_DATAA fd;
    snprintf(pat, sizeof pat, "%s\\*", from);
    HANDLE h = FindFirstFileA(pat, &fd); if (h == INVALID_HANDLE_VALUE) return;
    do if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        snprintf(a, sizeof a, "%s\\%s", from, fd.cFileName); snprintf(b, sizeof b, "%s\\%s", to, fd.cFileName); CopyFileA(a, b, FALSE);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}
static void write_bmp(const char *path, const unsigned char *bgra) {   // 320x200, 32-bit, top-down
    FILE *f = fopen(path, "wb"); if (!f) return;
    BITMAPFILEHEADER fh = { 0x4d42, sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + W * H * 4, 0, 0, sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) };
    BITMAPINFOHEADER ih = { sizeof ih, W, -H, 1, 32, BI_RGB, W * H * 4, 0, 0, 0, 0 };
    fwrite(&fh, sizeof fh, 1, f); fwrite(&ih, sizeof ih, 1, f); fwrite(bgra, W * H * 4, 1, f); fclose(f);
}
static void screenshot(const char *name, const unsigned char *bgra) {
    if (!session[0]) return;
    char t[16], p[MAX_PATH]; stamp(t, sizeof t, "%H.%M.%S");
    snprintf(p, sizeof p, "%s\\%s %s.bmp", session, name, t); write_bmp(p, bgra);
}
static int cmp_names(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }
static void prune(void) {                       // keep the newest 20 recordings (folders named like one, only)
    static char names[256][32]; int n = 0; char pat[MAX_PATH]; WIN32_FIND_DATAA fd;
    snprintf(pat, sizeof pat, "%s\\?\?\?\?-?\?-?\? ?\?.?\?.?\?", rec_root);
    HANDLE h = FindFirstFileA(pat, &fd); if (h == INVALID_HANDLE_VALUE) return;
    do if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && strlen(fd.cFileName) == 19 && n < 256) strcpy(names[n++], fd.cFileName); while (FindNextFileA(h, &fd));
    FindClose(h);
    qsort(names, n, sizeof names[0], cmp_names);
    for (int i = 0; i < n - 20; i++) {
        char dir[MAX_PATH + 2]; memset(dir, 0, sizeof dir); snprintf(dir, MAX_PATH, "%s\\%s", rec_root, names[i]);
        SHFILEOPSTRUCTA op = { 0, FO_DELETE, dir, NULL, FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT };
        SHFileOperationA(&op);
    }
}
static void rec_start(void) {
    if (!rec_enabled() || getenv("POOLRAD_NO_RECORDING")) return;
    char t[32], p[MAX_PATH]; stamp(t, sizeof t, "%Y-%m-%d %H.%M.%S");
    CreateDirectoryA(rec_root, NULL);
    snprintf(session, sizeof session, "%s\\%s", rec_root, t);
    snprintf(p, sizeof p, "%s\\start-saves", session);
    if (!CreateDirectoryA(session, NULL) || !CreateDirectoryA(p, NULL)) { session[0] = 0; return; }
    copy_files(save_dir, p);
    snprintf(p, sizeof p, "%s\\info.txt", session);
    FILE *f = fopen(p, "wb"); if (f) { fprintf(f, "Pool of Radiance session recording (Windows)\r\nbuild %s\r\nstarted %s\r\n", POOLRAD_BUILD, t); fclose(f); }
    if (!engine_record(session)) { session[0] = 0; return; }
    prune();
}
static void rec_mark(void) {
    char title[64] = "Pool of Radiance  (nothing marked: session recording is off)";
    if (session[0]) {
        mark_count++;
        char label[32], name[16], p[MAX_PATH]; snprintf(label, sizeof label, "player mark %d", mark_count); engine_mark(label);
        engine_copy_frame(frame); snprintf(name, sizeof name, "mark%02d", mark_count); screenshot(name, frame);
        snprintf(p, sizeof p, "%s\\mark%02d saves", session, mark_count); CreateDirectoryA(p, NULL); copy_files(save_dir, p);
        strcpy(title, "Pool of Radiance  (problem marked, thank you)");
    }
    SetWindowTextA(hwnd, title); SetTimer(hwnd, 2, 3000, NULL);
}
static void rec_notes(void) {                    // battle start, each round, battle over: the screen as it was then
    int n = engine_note_count(); if (n == notes_seen || !session[0]) return;
    notes_seen = n;
    static unsigned char nf[W * H * 4]; engine_note_frame(nf);
    char name[80]; snprintf(name, sizeof name, "%04d %s", n, engine_note_last()); screenshot(name, nf);
}

// ---------------------------------------------------------------- window and menu
static void toggle_fullscreen(void) {
    DWORD style = GetWindowLongA(hwnd, GWL_STYLE);
    if (!fullscreen) {
        MONITORINFO mi = { sizeof mi };
        GetWindowPlacement(hwnd, &saved_place);
        GetMonitorInfoA(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
        SetWindowLongA(hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
        SetMenu(hwnd, NULL);
        SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        SetWindowLongA(hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetMenu(hwnd, LoadMenuA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(IDM_MENU)));
        SetWindowPlacement(hwnd, &saved_place);
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    fullscreen = !fullscreen;
}
static void update_menu(void) {
    HMENU m = GetMenu(hwnd); if (!m) return;
    CheckMenuItem(m, ID_RECORD, MF_BYCOMMAND | (rec_enabled() ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, ID_ORIGINAL, MF_BYCOMMAND | (reg_get("OriginalBugs", 0) ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuRadioItem(m, ID_SMOOTH, ID_SHARP, sharp_pixels || !smooth_ok ? ID_SHARP : ID_SMOOTH, MF_BYCOMMAND);
    if (!smooth_ok) EnableMenuItem(m, ID_SMOOTH, MF_BYCOMMAND | MF_GRAYED);
}
static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_KEYDOWN: {
        if (wp == VK_F12) { rec_mark(); return 0; }
        int k = special_key(wp); if (k) engine_post_key((unsigned short)k);
        return 0;
    }
    case WM_CHAR: on_char(wp); return 0;
    case WM_SYSKEYDOWN:
        if (wp == VK_RETURN) { toggle_fullscreen(); return 0; }
        if (wp == VK_F4) break;
        { int k = special_key(wp); if (k) { engine_post_key((unsigned short)k); return 0; } }
        break;
    case WM_SYSCHAR:                               // Alt+letter: the BIOS gives its scan code with no character
        if (wp == 13) return 0;
        if (wp < 127 && scan_of((char)wp)) { engine_post_key((unsigned short)(scan_of((char)wp) << 8)); return 0; }
        break;
    case WM_SIZE: if (swap && wp != SIZE_MINIMIZED) d3d_resize(); return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO *)lp)->ptMinTrackSize.x = 480; ((MINMAXINFO *)lp)->ptMinTrackSize.y = 400; return 0;
    case WM_TIMER: if (wp == 2) { KillTimer(h, 2); SetWindowTextA(h, "Pool of Radiance"); } return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_MARK: rec_mark(); return 0;
        case ID_RECORD: rec_set(!rec_enabled()); update_menu(); return 0;
        case ID_ORIGINAL: reg_set("OriginalBugs", !reg_get("OriginalBugs", 0)); update_menu(); return 0;
        case ID_SHOW_RECORDINGS: CreateDirectoryA(rec_root, NULL); ShellExecuteA(h, "open", session[0] ? session : rec_root, NULL, NULL, SW_SHOWNORMAL); return 0;
        case ID_FULLSCREEN: toggle_fullscreen(); return 0;
        case ID_SMOOTH: sharp_pixels = 0; reg_set("SharpPixels", 0); update_menu(); return 0;
        case ID_SHARP: sharp_pixels = 1; reg_set("SharpPixels", 1); update_menu(); return 0;
        case ID_QUIT: PostMessageA(h, WM_CLOSE, 0, 0); return 0;
        case ID_ABOUT: MessageBoxA(h, "Pool of Radiance (SSI, 1988), ported to native C.\nWindows / Direct3D 12 build " POOLRAD_BUILD ".", "About Pool of Radiance", MB_ICONINFORMATION); return 0;
        }
        break;
    case WM_CLOSE: engine_request_quit(); DestroyWindow(h); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    (void)prev; (void)cmd;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    timeBeginPeriod(1);                            // 1 ms sleeps: the game's 236.7 Hz sound timer needs them
    char exe[MAX_PATH]; GetModuleFileNameA(NULL, exe, sizeof exe); *strrchr(exe, '\\') = 0;
    snprintf(game_dir, sizeof game_dir, "%s\\GameData", exe);
    const char *appdata = getenv("APPDATA"); if (!appdata) appdata = exe;
    snprintf(save_dir, sizeof save_dir, "%s\\Pool of Radiance", appdata);
    snprintf(rec_root, sizeof rec_root, "%s\\Pool of Radiance Recordings", appdata);
    if (getenv("POOLRAD_SAVE_DIR")) snprintf(save_dir, sizeof save_dir, "%s", getenv("POOLRAD_SAVE_DIR"));
    if (getenv("POOLRAD_RECORDINGS_DIR")) snprintf(rec_root, sizeof rec_root, "%s", getenv("POOLRAD_RECORDINGS_DIR"));
    CreateDirectoryA(save_dir, NULL);
    char probe[MAX_PATH]; snprintf(probe, sizeof probe, "%s\\POOLFLAT.EXE", game_dir);
    if (GetFileAttributesA(probe) == INVALID_FILE_ATTRIBUTES) fatal("The GameData folder is missing next to PoolOfRadiance.exe.");

    WNDCLASSEXA wc = { sizeof wc, CS_HREDRAW | CS_VREDRAW, wndproc, 0, 0, inst, LoadIconA(inst, MAKEINTRESOURCEA(IDI_APP)), LoadCursorA(NULL, IDC_ARROW), (HBRUSH)GetStockObject(BLACK_BRUSH), MAKEINTRESOURCEA(IDM_MENU), "PoolOfRadiance", NULL };
    RegisterClassExA(&wc);
    RECT r = { 0, 0, 960, 720 }; AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, TRUE, 0);
    hwnd = CreateWindowExA(0, "PoolOfRadiance", "Pool of Radiance", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    sharp_pixels = reg_get("SharpPixels", 0);
    d3d_init();
    update_menu();
    ShowWindow(hwnd, show);

    if (reg_get("OriginalBugs", 0)) _putenv_s("POOLRAD_ORIGINAL", "1");   // the original's bugs kept, if asked for (Debug menu)
    rec_start();
    engine_start(game_dir, save_dir);
    audio_init();
    MSG m;
    for (;;) {
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT) { timeEndPeriod(1); return 0; }
            TranslateMessage(&m); DispatchMessageA(&m);
        }
        if (engine_has_exited()) { DestroyWindow(hwnd); continue; }
        rec_notes();
        render();                                  // paced by Present's vertical sync
    }
}
