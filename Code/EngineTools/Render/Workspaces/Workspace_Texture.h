#pragma once

#include "EngineTools/Core/Workspace.h"
#include "System/Render/RenderTexture.h"
#include "System/Imgui/ImguiX.h"

//-------------------------------------------------------------------------

namespace EE::Render
{
    class TextureWorkspace final : public TWorkspace<Texture>
    {

    public:

        using TWorkspace::TWorkspace;

    private:

        virtual bool HasTitlebarIcon() const override { return true; }
        virtual char const* GetTitlebarIcon() const override { EE_ASSERT( HasTitlebarIcon() ); return EE_ICON_IMAGE_OUTLINE; }
        virtual bool HasViewportWindow() const { return false; }
        virtual void Initialize( UpdateContext const& context ) override;
        virtual void InitializeDockingLayout( ImGuiID dockspaceID ) const override;
        virtual void Update( UpdateContext const& context, ImGuiWindowClass* pWindowClass, bool isFocused ) override;

    private:

        String          m_previewWindowName;
        String          m_infoWindowName;
    };
}