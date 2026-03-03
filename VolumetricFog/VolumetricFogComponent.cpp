#include "VolumetricFogComponent.h"

#include <AzCore/Math/MathUtils.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Serialization/EditContext.h>

#if !defined(ECHO_SERVER)
#include <Atom/RPI.Public/Pass/PassFilter.h>
#include <Atom/RPI.Public/Pass/PassSystemInterface.h>
#include <Atom/RPI.Public/Pass/FullscreenTrianglePass.h>
#include <Atom/RPI.Public/Shader/ShaderResourceGroup.h>
#endif

namespace echo
{
    // Definition of the global pass-ownership pointer (declared extern in the header).
    // One pointer shared across the entire DLL — prevents two VolumetricFog entities
    // from racing to write different settings to the same pass.
    void* g_fogPassOwner = nullptr;

    // ============================================================
    // VolumetricFogSettings::Reflect
    // ============================================================
    void VolumetricFogSettings::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            // Guard against double-reflect (both VolumetricFogComponent and
            // EditorVolumetricFogComponent call this).
            if (serialize->FindClassData(azrtti_typeid<VolumetricFogSettings>()))
                return;

            serialize->Class<VolumetricFogSettings>()
                ->Version(5)  // v5: removed manual Beam Spot Light params; use clustered lighting
                ->Field("EnableFog",           &VolumetricFogSettings::m_enableFog)
                ->Field("FogColor",            &VolumetricFogSettings::m_fogColor)
                ->Field("FogDensity",          &VolumetricFogSettings::m_fogDensity)
                ->Field("FogHeightFalloff",    &VolumetricFogSettings::m_fogHeightFalloff)
                ->Field("FogBaseHeight",       &VolumetricFogSettings::m_fogBaseHeight)
                ->Field("FogStartDist",        &VolumetricFogSettings::m_fogStartDistance)
                ->Field("FogEndDist",          &VolumetricFogSettings::m_fogEndDistance)
                ->Field("FogMaxOpacity",       &VolumetricFogSettings::m_fogMaxOpacity)
                ->Field("FogNoiseScale",       &VolumetricFogSettings::m_fogNoiseScale)
                ->Field("FogNoiseSpeed",       &VolumetricFogSettings::m_fogNoiseSpeed)
                ->Field("FogSamples",          &VolumetricFogSettings::m_fogSamples)
                ->Field("ScatterAnisotropy",   &VolumetricFogSettings::m_scatterAnisotropy)
                ->Field("EnableGodRays",       &VolumetricFogSettings::m_enableGodRays)
                ->Field("GodRayIntensity",     &VolumetricFogSettings::m_godRayIntensity)
                ->Field("GodRayDecay",         &VolumetricFogSettings::m_godRayDecay)
                ->Field("GodRayDensity",       &VolumetricFogSettings::m_godRayDensity)
                ->Field("GodRayWeight",        &VolumetricFogSettings::m_godRayWeight)
                ->Field("GodRaySamples",       &VolumetricFogSettings::m_godRaySamples)
                ->Field("GodRayExposure",      &VolumetricFogSettings::m_godRayExposure)
                ->Field("GodRayThreshold",     &VolumetricFogSettings::m_godRayThreshold)
                ->Field("FogLightScatter",     &VolumetricFogSettings::m_fogLightScatter)
            ;

            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<VolumetricFogSettings>("Volumetric Fog Settings", "")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                        ->Attribute(AZ::Edit::Attributes::AutoExpand, true)

                    // ---- Fog ---------------------------------------------------
                    ->ClassElement(AZ::Edit::ClassElements::Group, "Fog")
                        ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::CheckBox,
                        &VolumetricFogSettings::m_enableFog, "Enable Fog", "")
                    ->DataElement(AZ::Edit::UIHandlers::Color,
                        &VolumetricFogSettings::m_fogColor, "Fog Color", "Tint color of the fog volume")
                    ->DataElement(AZ::Edit::UIHandlers::Slider,
                        &VolumetricFogSettings::m_fogDensity, "Density", "")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 10.0f)
                    ->DataElement(AZ::Edit::UIHandlers::Slider,
                        &VolumetricFogSettings::m_fogHeightFalloff, "Height Falloff", "")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 1.0f)
                    ->DataElement(AZ::Edit::UIHandlers::Default,
                        &VolumetricFogSettings::m_fogBaseHeight, "Base Height", "World-space height of the fog floor")
                    ->DataElement(AZ::Edit::UIHandlers::Default,
                        &VolumetricFogSettings::m_fogStartDistance, "Start Distance", "")
                    ->DataElement(AZ::Edit::UIHandlers::Default,
                        &VolumetricFogSettings::m_fogEndDistance, "End Distance", "")
                    ->DataElement(AZ::Edit::UIHandlers::Slider,
                        &VolumetricFogSettings::m_fogMaxOpacity, "Max Opacity", "")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 1.0f)

                    // ---- Volume / Noise ----------------------------------------
                    ->ClassElement(AZ::Edit::ClassElements::Group, "Volume / Noise")
                        ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Slider,
                        &VolumetricFogSettings::m_fogNoiseScale, "Noise Scale",
                        "World-space frequency. Smaller = larger cloud puffs.")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.001f)
                        ->Attribute(AZ::Edit::Attributes::Max, 0.5f)
                        ->Attribute(AZ::Edit::Attributes::Step, 0.001f)
                    ->DataElement(AZ::Edit::UIHandlers::Slider,
                        &VolumetricFogSettings::m_fogNoiseSpeed, "Drift Speed (m/s)", "")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 5.0f)
                        ->Attribute(AZ::Edit::Attributes::Step, 0.05f)
                    ->DataElement(AZ::Edit::UIHandlers::SpinBox,
                        &VolumetricFogSettings::m_fogSamples, "Ray Samples", "4-128 steps per ray")
                        ->Attribute(AZ::Edit::Attributes::Min, 4)
                        ->Attribute(AZ::Edit::Attributes::Max, 128)
                    ->DataElement(AZ::Edit::UIHandlers::Slider,
                        &VolumetricFogSettings::m_scatterAnisotropy, "Scatter Anisotropy (g)",
                        "Henyey-Greenstein g. 0=isotropic, 0.85=strong forward scatter.")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 0.99f)

                    // ---- Lighting ---------------------------------------------
                    ->ClassElement(AZ::Edit::ClassElements::Group, "Lighting")
                        ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Slider,
                        &VolumetricFogSettings::m_fogLightScatter, "Directional Light Scale",
                        "Scales sun/sky contribution to fog scatter. Default 0.003 = calibrated for ~100 klux direct sun.")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 0.05f)
                        ->Attribute(AZ::Edit::Attributes::Step, 0.0001f)

                    // ---- Legacy (collapsed by default) -----------------------
                    ->ClassElement(AZ::Edit::ClassElements::Group, "Legacy: God Rays (unused)")
                        ->Attribute(AZ::Edit::Attributes::AutoExpand, false)
                    ->DataElement(AZ::Edit::UIHandlers::CheckBox,
                        &VolumetricFogSettings::m_enableGodRays, "Enable God Rays",
                        "Legacy flag — not used by the clustered fog shader.")
                    ->DataElement(AZ::Edit::UIHandlers::Slider,
                        &VolumetricFogSettings::m_godRayIntensity, "Intensity", "")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 10.0f)
                ;
            }
        }
    }

    // ============================================================
    // VolumetricFogComponent::Reflect
    // ============================================================
    void VolumetricFogComponent::Reflect(AZ::ReflectContext* context)
    {
        VolumetricFogSettings::Reflect(context);

        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<VolumetricFogComponent, AZ::Component>()
                ->Version(1)
                ->Field("Settings", &VolumetricFogComponent::m_settings)
            ;

            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<VolumetricFogComponent>(
                    "Volumetric Fog", "Atmospheric fog using clustered scene lighting")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                        ->Attribute(AZ::Edit::Attributes::Category, "Echo/Rendering")
                        ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                        ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default,
                        &VolumetricFogComponent::m_settings, "Fog Settings", "")
                ;
            }
        }
    }

    void VolumetricFogComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("VolumetricFogService"));
    }

    void VolumetricFogComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("VolumetricFogService"));
    }

    // ============================================================
    // Lifecycle
    // ============================================================
    void VolumetricFogComponent::Activate()
    {
        m_passReady = false;
        VolumetricFogRequestBus::Handler::BusConnect(GetEntityId());
        AZ::TickBus::Handler::BusConnect();
    }

    void VolumetricFogComponent::Deactivate()
    {
        AZ::TickBus::Handler::BusDisconnect();
        VolumetricFogRequestBus::Handler::BusDisconnect();
        TurnOffPass();
        FogPassRelease(this);
        m_passReady = false;
    }

    void VolumetricFogComponent::TurnOffPass()
    {
#if !defined(ECHO_SERVER)
        if (!FogPassIsOwner(this))
            return;

        AZ::RPI::PassFilter filter = AZ::RPI::PassFilter::CreateWithPassName(
            AZ::Name("VolumetricFogPass"), static_cast<AZ::RPI::Scene*>(nullptr));

        AZ::RPI::PassSystemInterface::Get()->ForEachPass(filter,
            [](AZ::RPI::Pass* pass) -> AZ::RPI::PassFilterExecutionFlow
            {
                pass->SetEnabled(false);
                return AZ::RPI::PassFilterExecutionFlow::ContinueVisitingPasses;
            });
#endif
    }

    void VolumetricFogComponent::OnTick(float deltaTime, AZ::ScriptTimePoint /*time*/)
    {
        m_time = fmod(m_time + deltaTime, 3600.0f);

        if (UpdatePassParameters())
            m_passReady = true;
    }

    // ============================================================
    // UpdatePassParameters
    // ============================================================
    bool VolumetricFogComponent::UpdatePassParameters()
    {
#if !defined(ECHO_SERVER)
        if (!FogPassIsOwner(this))
        {
            if (!FogPassTryAcquire(this))
            {
                AZ_TracePrintf("VolumetricFog",
                    "UpdatePassParameters: another VolumetricFog component owns the pass — skipping.");
                return false;
            }
        }

        // ---- Fog base -------------------------------------------------------
        bool    passEnabled = m_settings.m_enableFog;
        int     enableFog   = m_settings.m_enableFog ? 1 : 0;
        AZ::Vector3 fogColor(m_settings.m_fogColor.GetR(),
                             m_settings.m_fogColor.GetG(),
                             m_settings.m_fogColor.GetB());
        float  fogDensity   = m_settings.m_fogDensity;
        float  fogHeight    = m_settings.m_fogHeightFalloff;
        float  fogBase      = m_settings.m_fogBaseHeight;
        float  fogStart     = m_settings.m_fogStartDistance;
        float  fogEnd       = m_settings.m_fogEndDistance;
        float  fogOpacity   = m_settings.m_fogMaxOpacity;
        float  noiseScale   = m_settings.m_fogNoiseScale;
        float  noiseSpeed   = m_settings.m_fogNoiseSpeed;
        float  curTime      = m_time;
        int    fogSamples   = m_settings.m_fogSamples;
        float  anisotropy   = m_settings.m_scatterAnisotropy;

        // ---- Legacy god-ray passthrough (kept in SRG for compat) -----------
        int    enableGodRays = m_settings.m_enableGodRays ? 1 : 0;
        float  rayIntensity  = m_settings.m_godRayIntensity;
        float  rayDecay      = m_settings.m_godRayDecay;
        float  rayDensity    = m_settings.m_godRayDensity;
        float  rayWeight     = m_settings.m_godRayWeight;
        int    raySamples    = m_settings.m_godRaySamples;
        float  rayExposure   = m_settings.m_godRayExposure;
        float  rayThreshold  = m_settings.m_godRayThreshold;
        float  lightScatter  = m_settings.m_fogLightScatter;

        // ---- Push to all VolumetricFogPass instances (editor + PIE) --------
        bool anyUpdated = false;

        AZ::RPI::PassFilter filter = AZ::RPI::PassFilter::CreateWithPassName(
            AZ::Name("VolumetricFogPass"), static_cast<AZ::RPI::Scene*>(nullptr));

        AZ::RPI::PassSystemInterface::Get()->ForEachPass(filter,
            [&](AZ::RPI::Pass* pass) -> AZ::RPI::PassFilterExecutionFlow
            {
                pass->SetEnabled(passEnabled);

                auto* fp = azrtti_cast<AZ::RPI::FullscreenTrianglePass*>(pass);
                if (!fp) return AZ::RPI::PassFilterExecutionFlow::ContinueVisitingPasses;

                auto srg = fp->GetShaderResourceGroup();
                if (!srg) return AZ::RPI::PassFilterExecutionFlow::ContinueVisitingPasses;

                // Verify the SRG has our known constant before writing
                AZ::RHI::ShaderInputConstantIndex fogColorIdx =
                    srg->FindShaderInputConstantIndex(AZ::Name("m_fogColor"));
                if (fogColorIdx.IsNull())
                    return AZ::RPI::PassFilterExecutionFlow::ContinueVisitingPasses;

                // Fog base
                srg->SetConstant(fogColorIdx, fogColor);
                srg->SetConstant(srg->FindShaderInputConstantIndex(AZ::Name("m_fogDensity")),       fogDensity);
                srg->SetConstant(srg->FindShaderInputConstantIndex(AZ::Name("m_fogHeightFalloff")), fogHeight);
                srg->SetConstant(srg->FindShaderInputConstantIndex(AZ::Name("m_fogBaseHeight")),    fogBase);
                srg->SetConstant(srg->FindShaderInputConstantIndex(AZ::Name("m_fogStartDistance")), fogStart);
                srg->SetConstant(srg->FindShaderInputConstantIndex(AZ::Name("m_fogEndDistance")),   fogEnd);
                srg->SetConstant(srg->FindShaderInputConstantIndex(AZ::Name("m_fogMaxOpacity")),    fogOpacity);
                srg->SetConstant(srg->FindShaderInputConstantIndex(AZ::Name("m_enableFog")),        enableFog);
                // Noise
                srg->SetConstant(srg->FindShaderInputConstantIndex(AZ::Name("m_fogNoiseScale")),    noiseScale);
                srg->SetConstant(srg->FindShaderInputConstantIndex(AZ::Name("m_fogNoiseSpeed")),    noiseSpeed);
                srg->SetConstant(srg->FindShaderInputConstantIndex(AZ::Name("m_time")),             curTime);
                srg->SetConstant(srg->FindShaderInputConstantIndex(AZ::Name("m_fogSamples")),       fogSamples);
                srg->SetConstant(srg->FindShaderInputConstantIndex(AZ::Name("m_scatterAnisotropy")),anisotropy);
                // Legacy god-ray passthrough
                srg->SetConstant(srg->FindShaderInputConstantIndex(AZ::Name("m_godRayIntensity")),  rayIntensity);
                srg->SetConstant(srg->FindShaderInputConstantIndex(AZ::Name("m_godRayDecay")),      rayDecay);
                srg->SetConstant(srg->FindShaderInputConstantIndex(AZ::Name("m_godRayDensity")),    rayDensity);
                srg->SetConstant(srg->FindShaderInputConstantIndex(AZ::Name("m_godRayWeight")),     rayWeight);
                srg->SetConstant(srg->FindShaderInputConstantIndex(AZ::Name("m_godRaySamples")),    raySamples);
                srg->SetConstant(srg->FindShaderInputConstantIndex(AZ::Name("m_godRayExposure")),   rayExposure);
                srg->SetConstant(srg->FindShaderInputConstantIndex(AZ::Name("m_enableGodRays")),    enableGodRays);
                srg->SetConstant(srg->FindShaderInputConstantIndex(AZ::Name("m_godRayThreshold")),  rayThreshold);
                srg->SetConstant(srg->FindShaderInputConstantIndex(AZ::Name("m_fogLightScatter")),  lightScatter);

                anyUpdated = true;
                return AZ::RPI::PassFilterExecutionFlow::ContinueVisitingPasses;
            });

        if (!anyUpdated)
            AZ_TracePrintf("VolumetricFog", "UpdatePassParameters: pass 'VolumetricFogPass' not found");

        return anyUpdated;
#else
        return false;
#endif
    }

    // ============================================================
    // EBus handlers
    // ============================================================
    void VolumetricFogComponent::SetFogEnabled(bool v)            { m_settings.m_enableFog = v;            if (m_passReady) UpdatePassParameters(); }
    void VolumetricFogComponent::SetFogDensity(float v)           { m_settings.m_fogDensity = v;           if (m_passReady) UpdatePassParameters(); }
    void VolumetricFogComponent::SetFogColor(const AZ::Color& v)  { m_settings.m_fogColor = v;             if (m_passReady) UpdatePassParameters(); }
    void VolumetricFogComponent::SetFogHeightFalloff(float v)     { m_settings.m_fogHeightFalloff = v;     if (m_passReady) UpdatePassParameters(); }
    void VolumetricFogComponent::SetFogBaseHeight(float v)        { m_settings.m_fogBaseHeight = v;        if (m_passReady) UpdatePassParameters(); }
    void VolumetricFogComponent::SetFogStartDistance(float v)     { m_settings.m_fogStartDistance = v;     if (m_passReady) UpdatePassParameters(); }
    void VolumetricFogComponent::SetFogEndDistance(float v)       { m_settings.m_fogEndDistance = v;       if (m_passReady) UpdatePassParameters(); }
    void VolumetricFogComponent::SetFogMaxOpacity(float v)        { m_settings.m_fogMaxOpacity = v;        if (m_passReady) UpdatePassParameters(); }
    void VolumetricFogComponent::SetGodRaysEnabled(bool v)        { m_settings.m_enableGodRays = v;        if (m_passReady) UpdatePassParameters(); }
    void VolumetricFogComponent::SetGodRayIntensity(float v)      { m_settings.m_godRayIntensity = v;      if (m_passReady) UpdatePassParameters(); }
    void VolumetricFogComponent::SetGodRaySamples(int v)          { m_settings.m_godRaySamples = v;        if (m_passReady) UpdatePassParameters(); }

} // namespace echo
