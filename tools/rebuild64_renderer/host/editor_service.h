#pragma once
#include <d3dcompiler.h>
#include <d3d11_1.h>
// Host composition only. Never changes the renderer-owned output/depth/policy.
struct EditorService {
    float clip[16]{};
    uint64_t serial=0,generation=0;
    double cpuMs=0;
    UINT width=0,height=0,stride=0;
    std::vector<uint8_t> pixels;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> ps;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blend;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth;
    Microsoft::WRL::ComPtr<ID3DDeviceContextState> state;
    HRESULT draw(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* target,uint64_t current) {
        D3D11_TEXTURE2D_DESC desc{};target->GetDesc(&desc);
        if(pixels.empty()||generation!=current||width!=desc.Width||height!=desc.Height)return S_OK;
        HRESULT hr=S_OK;
        if(!vs) {
            const char code[]= "Texture2D<float4> image:register(t0);struct V{float4 p:SV_POSITION;};V vertex(uint i:SV_VertexID){V o;o.p=float4(i==1?3:-1,i==2?-3:1,0,1);return o;}float4 pixel(V i):SV_TARGET{return image.Load(int3(i.p.xy,0));}";
            Microsoft::WRL::ComPtr<ID3DBlob> vertex,pixel,error;
            hr=D3DCompile(code,sizeof(code)-1,nullptr,nullptr,nullptr,"vertex","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vertex,&error);if(FAILED(hr))return hr;
            hr=D3DCompile(code,sizeof(code)-1,nullptr,nullptr,nullptr,"pixel","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&pixel,&error);if(FAILED(hr))return hr;
            hr=device->CreateVertexShader(vertex->GetBufferPointer(),vertex->GetBufferSize(),nullptr,&vs);if(FAILED(hr))return hr;
            hr=device->CreatePixelShader(pixel->GetBufferPointer(),pixel->GetBufferSize(),nullptr,&ps);if(FAILED(hr))return hr;
            D3D11_BLEND_DESC b{};auto& r=b.RenderTarget[0];r.BlendEnable=TRUE;r.SrcBlend=D3D11_BLEND_ONE;r.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;r.BlendOp=D3D11_BLEND_OP_ADD;r.SrcBlendAlpha=D3D11_BLEND_ONE;r.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;r.BlendOpAlpha=D3D11_BLEND_OP_ADD;r.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
            hr=device->CreateBlendState(&b,&blend);if(FAILED(hr))return hr;
            D3D11_RASTERIZER_DESC ra{};ra.FillMode=D3D11_FILL_SOLID;ra.CullMode=D3D11_CULL_NONE;ra.DepthClipEnable=TRUE;
            hr=device->CreateRasterizerState(&ra,&raster);if(FAILED(hr))return hr;
            D3D11_DEPTH_STENCIL_DESC ds{};ds.DepthEnable=FALSE;hr=device->CreateDepthStencilState(&ds,&depth);if(FAILED(hr))return hr;
        }
        D3D11_TEXTURE2D_DESC old{};if(texture)texture->GetDesc(&old);
        if(!texture||old.Width!=width||old.Height!=height) {
            texture.Reset();view.Reset();D3D11_TEXTURE2D_DESC t{};t.Width=width;t.Height=height;t.MipLevels=1;t.ArraySize=1;t.Format=DXGI_FORMAT_R8G8B8A8_UNORM;t.SampleDesc.Count=1;t.Usage=D3D11_USAGE_DEFAULT;t.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            hr=device->CreateTexture2D(&t,nullptr,&texture);if(FAILED(hr))return hr;
            hr=device->CreateShaderResourceView(texture.Get(),nullptr,&view);if(FAILED(hr))return hr;
        }
        context->UpdateSubresource(texture.Get(),0,nullptr,pixels.data(),stride,0);
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> output;
        hr=device->CreateRenderTargetView(target,nullptr,&output);if(FAILED(hr))return hr;
        Microsoft::WRL::ComPtr<ID3D11Device1> device1;Microsoft::WRL::ComPtr<ID3D11DeviceContext1> context1;
        hr=device->QueryInterface(IID_PPV_ARGS(&device1));if(FAILED(hr))return hr;
        hr=context->QueryInterface(IID_PPV_ARGS(&context1));if(FAILED(hr))return hr;
        if(!state){const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0};hr=device1->CreateDeviceContextState(0,levels,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),nullptr,&state);if(FAILED(hr))return hr;}
        struct Restore {ID3D11DeviceContext1* context;Microsoft::WRL::ComPtr<ID3DDeviceContextState> previous;~Restore(){context->SwapDeviceContextState(previous.Get(),nullptr);}} restore{context1.Get(),{}};
        context1->SwapDeviceContextState(state.Get(),&restore.previous);
        ID3D11RenderTargetView* outputs[]={output.Get()};context->OMSetRenderTargets(1,outputs,nullptr);
        context->OMSetBlendState(blend.Get(),nullptr,0xffffffff);context->OMSetDepthStencilState(depth.Get(),0);
        D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};context->RSSetViewports(1,&viewport);context->RSSetState(raster.Get());
        context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);context->GSSetShader(nullptr,nullptr,0);
        ID3D11ShaderResourceView* views[]={view.Get()};context->PSSetShaderResources(0,1,views);context->Draw(3,0);
        views[0]=nullptr;context->PSSetShaderResources(0,1,views);context->OMSetRenderTargets(0,nullptr,nullptr);
        return S_OK;
    }
};
