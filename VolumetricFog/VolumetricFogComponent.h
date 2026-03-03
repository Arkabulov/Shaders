#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/Component/EntityId.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Math/Color.h>
#include <AzCore/Math/Vector3.h>

namespace echo
{
    // -----------------------------------------------------------------------
    // Pass ownership — only ONE VolumetricFog component drives the pass at a time.
    //
    // Without this, two entities with VolumetricFog (accidentally created, or
    // during PIE transition) race to push settings to the same shared pass,
    // causing apparent "settings reset" when a second entity activates.
    //
    // Rules:
    //   • Activate()              — call TryAcquire(this); if false, log warning.
    //   • Deactivate()            — call Release(this).
    //   • UpdatePassParameters()  — skip if !IsOwner(this), try TryAcquire first.
    //   • TurnOffPass()           — skip if !IsOwner(this).
    //
    // Defined in VolumetricFogComponent.cpp (shared across the DLL).
    // -----------------------------------------------------------------------
    extern void* g_fogPassOwner;  // nullptr = no owner

    inline bool FogPassTryAcquire(void* comp)
    {
        if (g_fogPassOwner == nullptr) { g_fogPassOwner = comp; return true; }
        return g_fogPassOwner == comp;
    }
    inline void FogPassRelease(void* comp)
    {
        if (g_fogPassOwner == comp) g_fogPassOwner = nullptr;
    }
    inline bool FogPassIsOwner(void* comp) { return g_fogPassOwner == comp; }

    class VolumetricFogRequests : public AZ::EBusTraits
    {
    public:
        static const AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::ById;
        using BusIdType = AZ::EntityId;

        virtual void SetFogEnabled(bool enabled) = 0;
        virtual void SetFogDensity(float density) = 0;
        virtual void SetFogColor(const AZ::Color& color) = 0;
        virtual void SetFogHeightFalloff(float falloff) = 0;
        virtual void SetFogBaseHeight(float height) = 0;
        virtual void SetFogStartDistance(float dist) = 0;
        virtual void SetFogEndDistance(float dist) = 0;
        virtual void SetFogMaxOpacity(float opacity) = 0;
        virtual void SetGodRaysEnabled(bool enabled) = 0;
        virtual void SetGodRayIntensity(float intensity) = 0;
        virtual void SetGodRaySamples(int samples) = 0;
    };

    using VolumetricFogRequestBus = AZ::EBus<VolumetricFogRequests>;

    struct VolumetricFogSettings
    {
        // UUID distinct from VolumetricFogComponent and EditorVolumetricFogComponent.
        AZ_TYPE_INFO(VolumetricFogSettings, "{3F8A1C9D-72B4-4E6F-A5D0-8C3B9F2E1A7C}");
        AZ_CLASS_ALLOCATOR(VolumetricFogSettings, AZ::SystemAllocator, 0);

        static void Reflect(AZ::ReflectContext* context);

        // ---- Fog base -------------------------------------------------------
        bool        m_enableFog         = true;
        AZ::Color   m_fogColor          = AZ::Color(0.82f, 0.87f, 0.92f, 1.0f);
        float       m_fogDensity        = 1.5f;
        float       m_fogHeightFalloff  = 0.05f;
        float       m_fogBaseHeight     = 0.0f;
        float       m_fogStartDistance  = 2.0f;
        float       m_fogEndDistance    = 120.0f;
        float       m_fogMaxOpacity     = 0.92f;

        // ---- Noise ----------------------------------------------------------
        float       m_fogNoiseScale     = 0.025f;
        float       m_fogNoiseSpeed     = 1.0f;
        int         m_fogSamples        = 32;

        // ---- Scatter anisotropy (Henyey-Greenstein g) -----------------------
        float       m_scatterAnisotropy = 0.85f;

        // ---- God-ray / directional scatter legacy ---------------------------
        // Sun direction / color are read from SceneSrg directional lights.
        // These fields are kept for legacy level compatibility.
        bool        m_enableGodRays     = false;
        float       m_godRayIntensity   = 2.5f;
        float       m_godRayDecay       = 0.97f;
        float       m_godRayDensity     = 0.96f;
        float       m_godRayWeight      = 0.5f;
        int         m_godRaySamples     = 64;
        float       m_godRayExposure    = 0.35f;
        float       m_godRayThreshold   = 0.3f;
        float       m_fogLightScatter   = 0.003f;
    };

    class VolumetricFogComponent
        : public AZ::Component
        , public VolumetricFogRequestBus::Handler
        , public AZ::TickBus::Handler
    {
    public:
        // UUID distinct from VolumetricFogSettings and EditorVolumetricFogComponent.
        AZ_COMPONENT(VolumetricFogComponent, "{7A4D2E5F-B8C1-4F9A-82E3-C6D0A1B9F3E5}");

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);

        void Activate() override;
        void Deactivate() override;

        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;

        // VolumetricFogRequestBus
        void SetFogEnabled(bool enabled) override;
        void SetFogDensity(float density) override;
        void SetFogColor(const AZ::Color& color) override;
        void SetFogHeightFalloff(float falloff) override;
        void SetFogBaseHeight(float height) override;
        void SetFogStartDistance(float dist) override;
        void SetFogEndDistance(float dist) override;
        void SetFogMaxOpacity(float opacity) override;
        void SetGodRaysEnabled(bool enabled) override;
        void SetGodRayIntensity(float intensity) override;
        void SetGodRaySamples(int samples) override;

        // Called from EditorVolumetricFogComponent::BuildGameEntity
        void InitSettings(const VolumetricFogSettings& settings) { m_settings = settings; }

    private:
        bool UpdatePassParameters();
        void TurnOffPass();

        VolumetricFogSettings m_settings;
        bool  m_passReady = false;
        float m_time      = 0.0f;
    };

} // namespace echo
