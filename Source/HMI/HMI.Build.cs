using System;
using System.IO;
using UnrealBuildTool;

public class HMI : ModuleRules
{
    public HMI(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false;
        bEnableExceptions = true;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "CoreUObject",
            "Engine",
            "UMG",
            "Projects", // IPluginManager

            "AudioPlatformConfiguration", // FResampler
            "SignalProcessing", // ArrayPcm16ToFloat
            "Voice", // CreateVoiceCapture
            "WebRTC", // webrtc::AudioProcessing
            
            "HTTP", // IHttpRequest
            "Json",
            "JsonUtilities",

            // FHMIHttpRequest
            "libcurl",
            "zlib", // libcurl dep
            "nghttp2", // libcurl dep
        });

        AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL"); // libcurl dep
    }
}
