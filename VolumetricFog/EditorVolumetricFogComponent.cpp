#ifdef ECHO_EDITOR
#include "EditorVolumetricFogComponent.h"

#include <AzCore/Math/MathUtils.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Serialization/EditContext.h>

#include <Atom/RPI.Public/Pass/PassFilter.h>
#include <Atom/RPI.Public/Pass/PassSystemInterface.h>
#include <Atom/RPI.Public/Pass/FullscreenTrianglePass.h>
#include <Atom/RPI.Public/Shader/ShaderResourceGroup.h>

namespace echo
{
    // ============================================================
    // Reflect
    // ============================================================
    void EditorVolumetricFogComponent::Reflect(AZ::ReflectContext* context)
    {
        // VolumetricFogSettings is reflected by VolumetricFogComponent::Reflect.
        // Double-call is guarded inside VolumetricFogSettings::Reflect.
        VolumetricFogSettings::Reflect(context);

        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<EditorVolumetricFogComponent, EditorComponentBase>()
                ->Version(1)
                ->Field("Settings", &EditorVolumetricFogComponent::m_settings)
            ;

            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<EditorVolumetricFogComponent>(
                    "Volumetric Fog", "Atmospheric fog using clustered scene lighting (editor preview)")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                        ->Attribute(AZ::Edit::Attributes::Category, "Echo/Rendering")
                        ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                        ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default,
                        &EditorVolumetricFogComponent::m_settings, "Fog Settings", "")
                ;
            }
        }
    }

    void EditorVolumetricFogComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("VolumetricFogService"));
    }

    void EditorVolumetricFogComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("VolumetricFogService"));
    }

    // ============================================================
    // Lifecycle
    // ============================================================
    void EditorVolumetricFogComponent::Activate()
    {
        EditorComponentBase::Activate();
        m_passReady          = false;
        m_time               = 0.0f;
        m_buildingGameEntity = false;
        AZ::TickBus::Handler::BusConnect();
    }

    void EditorVolumetricFogComponent::Deactivate()
    {
        AZ::TickBus::Handler::BusDisconnect();

        if (!m_buildingGameEntity)
            TurnOffPass();
        // else: PIE starting — game component takes over immediately.

        FogPassRelease(this);
        m_passReady          = false;
        m_buildingGameEntity = false;
        EditorComponentBase::Deactivate();
    }

    void EditorVolumetricFogComponent::TurnOffPass()
    {
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
    }

    void EditorVolumetricFogComponent::OnTick(float deltaTime, AZ::ScriptTimePoint /*time*/)
    {
        m_time = fmod(m_time + deltaTime, 3600.0f);

        if (UpdatePassParameters())
            m_passReady = true;
    }

    // ============================================================
    // BuildGameEntity
    // ============================================================
    void EditorVolumetricFogComponent::BuildGameEntity(AZ::Entity* gameEntity)
    {
        m_buildingGameEntity = true;

        auto* gameComp = gameEntity->CreateComponent<VolumetricFogComponent>();
        if (gameComp)
            gameComp->InitSettings(m_settings);
    }

    // ============================================================
    // UpdatePassParameters
    // Identical SRG-binding logic to VolumetricFogComponent but
    // without ECHO_SERVER guard (editor always has render).
    // ============================================================
    bool EditorVolumetricFogComponent::UpdatePassParameters()
    {
        if (!FogPassIsOwner(this))
        {
            if (!FogPassTryAcquire(this))
                return false;
        }

        // ---- Fog base -------------------------------------------------------
        bool    passEnabled  = m_settings.m_enableFog;
        int     enableFog    = m_settings.m_enableFog ? 1 : 0;
        AZ::Vector3 fogColor(m_settings.m_fogColor.GetR(),
                             m_settings.m_fogColor.GetG(),
                             m_settings.m_fogColor.GetB());
        float  fogDensity    = m_settings.m_fogDensity;
        float  fogHeight     = m_settings.m_fogHeightFalloff;
        float  fogBase       = m_settings.m_fogBaseHeight;
        float  fogStart      = m_settings.m_fogStartDistance;
        float  fogEnd        = m_settings.m_fogEndDistance;
        float  fogOpacity    = m_settings.m_fogMaxOpacity;
        float  noiseScale    = m_settings.m_fogNoiseScale;
        float  noiseSpeed    = m_settings.m_fogNoiseSpeed;
        float  curTime       = m_time;
        int    fogSamples    = m_settings.m_fogSamples;
        float  anisotropy    = m_settings.m_scatterAnisotropy;

        // ---- Legacy god-ray passthrough -------------------------------------
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

        return anyUpdated;
    }

} // namespace echo
#endif // ECHO_EDITOR
