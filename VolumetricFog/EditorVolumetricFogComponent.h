#pragma once
#ifdef ECHO_EDITOR

#include "VolumetricFogComponent.h"

#include <AzCore/Component/TickBus.h>
#include <AzToolsFramework/ToolsComponents/EditorComponentBase.h>

namespace echo
{
    // -----------------------------------------------------------------------
    // EditorVolumetricFogComponent
    //
    // Editor wrapper around VolumetricFogComponent.
    // Active in editor viewport — updates the SRG every tick so the fog is
    // visible without entering PIE.
    //
    // BuildGameEntity() copies settings into a VolumetricFogComponent when
    // the game process starts.
    // -----------------------------------------------------------------------
    class EditorVolumetricFogComponent
        : public AzToolsFramework::Components::EditorComponentBase
        , public AZ::TickBus::Handler
    {
    public:
        // UUID distinct from VolumetricFogSettings and VolumetricFogComponent.
        AZ_EDITOR_COMPONENT(EditorVolumetricFogComponent,
            "{9B5C3F8A-E2D4-4A1B-C7F6-2D0E9A8B5C31}",
            AzToolsFramework::Components::EditorComponentBase);

        // AZ::Component
        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);

        void Activate() override;
        void Deactivate() override;

        // AZ::TickBus::Handler
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;

        // AzToolsFramework::Components::EditorComponentBase
        void BuildGameEntity(AZ::Entity* gameEntity) override;

    private:
        VolumetricFogSettings m_settings;

        float m_time               = 0.0f;
        bool  m_passReady          = false;

        // Set to true in BuildGameEntity() so Deactivate() does not disable the
        // pass — the game component is about to take over.
        bool  m_buildingGameEntity = false;

        bool UpdatePassParameters();
        void TurnOffPass();
    };

} // namespace echo
#endif // ECHO_EDITOR
