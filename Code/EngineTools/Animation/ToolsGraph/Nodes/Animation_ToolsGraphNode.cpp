#include "Animation_ToolsGraphNode.h"

//-------------------------------------------------------------------------

namespace EE::Animation::GraphNodes
{
    constexpr static float const g_playbackBarMinimumWidth = 120;
    constexpr static float const g_playbackBarHeight = 10;
    constexpr static float const g_playbackBarMarkerSize = 4;
    constexpr static float const g_playbackBarRegionHeight = g_playbackBarHeight + g_playbackBarMarkerSize;

    void DrawPoseNodeDebugInfo( VisualGraph::DrawContext const& ctx, float width, PoseNodeDebugInfo const& debugInfo )
    {
        float const availableWidth = Math::Max( width, g_playbackBarMinimumWidth );
        ImVec2 const playbackBarSize = ImVec2( availableWidth, g_playbackBarHeight );
        ImVec2 const playbackBarTopLeft = ImGui::GetCursorScreenPos();
        ImVec2 const playbackBarBottomRight = playbackBarTopLeft + playbackBarSize;

        Percentage const percentageThroughTrack = debugInfo.m_currentTime.GetNormalizedTime();
        float const pixelOffsetForPercentageThrough = Math::Floor( playbackBarSize.x * percentageThroughTrack );

        //-------------------------------------------------------------------------

        // Draw spacer
        ImVec2 const playbackBarRegion = ImVec2( availableWidth, g_playbackBarRegionHeight );
        ImGui::InvisibleButton( "Spacer", playbackBarRegion );

        // Draw events
        bool useAlternateColor = false;
        ImVec2 eventTopLeft = playbackBarTopLeft;
        ImVec2 eventBottomRight = playbackBarBottomRight;
        for ( auto const& evt : debugInfo.m_pSyncTrack->GetEvents() )
        {
            eventBottomRight.x = eventTopLeft.x + Math::Floor( playbackBarSize.x * evt.m_duration );
            ctx.m_pDrawList->AddRectFilled( eventTopLeft, eventBottomRight, useAlternateColor ? ImGuiX::ImColors::White : ImGuiX::ImColors::DarkGray );
            eventTopLeft.x = eventBottomRight.x;
            useAlternateColor = !useAlternateColor;
        }

        // Draw progress bar
        ImVec2 progressBarTopLeft = playbackBarTopLeft;
        ImVec2 progressBarBottomRight = playbackBarTopLeft + ImVec2( pixelOffsetForPercentageThrough, playbackBarSize.y );
        ctx.m_pDrawList->AddRectFilled( progressBarTopLeft, progressBarBottomRight, ImGuiX::ToIm( Colors::LimeGreen.GetAlphaVersion( 0.65f ) ) );

        // Draw Marker
        ImVec2 t0( progressBarTopLeft.x + pixelOffsetForPercentageThrough, playbackBarBottomRight.y );
        ImVec2 t1( t0.x - g_playbackBarMarkerSize, playbackBarBottomRight.y + g_playbackBarMarkerSize );
        ImVec2 t2( t0.x + g_playbackBarMarkerSize, playbackBarBottomRight.y + g_playbackBarMarkerSize );
        ctx.m_pDrawList->AddLine( t0, t0 - ImVec2( 0, playbackBarSize.y ), ImGuiX::ImColors::LimeGreen );
        ctx.m_pDrawList->AddTriangleFilled( t0, t1, t2, ImGuiX::ImColors::LimeGreen );

        // Draw text info
        ImGui::Text( "Time: %.2f/%.2fs", debugInfo.m_currentTime.ToFloat() * debugInfo.m_duration, debugInfo.m_duration.ToFloat() );
        ImGui::Text( "Percent: %.1f%%", debugInfo.m_currentTime.ToFloat() * 100 );
        ImGui::Text( "Event: %d, %.1f%%", debugInfo.m_currentSyncTime.m_eventIdx, debugInfo.m_currentSyncTime.m_percentageThrough.ToFloat() * 100 );
        StringID const eventID = debugInfo.m_pSyncTrack->GetEventID( debugInfo.m_currentSyncTime.m_eventIdx );
        ImGui::Text( "Event ID: %s", eventID.IsValid() ? eventID.c_str() : "No ID");
    }

    void DrawEmptyPoseNodeDebugInfo( VisualGraph::DrawContext const& ctx, float width )
    {
        float const availableWidth = Math::Max( width, g_playbackBarMinimumWidth );
        ImVec2 const playbackBarSize = ImVec2( availableWidth, g_playbackBarHeight );
        ImVec2 const playbackBarTopLeft = ImGui::GetCursorScreenPos();
        ImVec2 const playbackBarBottomRight = playbackBarTopLeft + playbackBarSize;

        // Draw spacer
        ImVec2 const playbackBarRegion = ImVec2( availableWidth, g_playbackBarRegionHeight );
        ImGui::InvisibleButton( "Spacer", playbackBarRegion );

        // Draw empty playback visualization bar
        ctx.m_pDrawList->AddRectFilled( playbackBarTopLeft, playbackBarTopLeft + playbackBarSize, ImGuiX::ImColors::DarkGray );

        // Draw text placeholders
        ImGui::Text( "Time: N/A" );
        ImGui::Text( "Percent: N/A" );
        ImGui::Text( "Event: N/A" );
        ImGui::Text( "Event ID: N/A" );
    }

    void DrawVectorInfoText( VisualGraph::DrawContext const& ctx, Vector const& value )
    {
        ImGui::Text( "X: %.2f, Y: %.2f, Z: %.2f, W: %.2f", value.m_x, value.m_y, value.m_z, value.m_w );
    }

    void DrawTargetInfoText( VisualGraph::DrawContext const& ctx, Target const& value )
    {
        if ( value.IsTargetSet() )
        {
            if ( value.IsBoneTarget() )
            {
                if ( value.GetBoneID().IsValid() )
                {
                    ImGui::Text( "Value: %s", value.GetBoneID().c_str() );
                }
                else
                {
                    ImGui::Text( "Value: Invalid" );
                }
            }
            else
            {
                Transform const& transform = value.GetTransform();
                Vector const& translation = transform.GetTranslation();
                EulerAngles const angles = transform.GetRotation().ToEulerAngles();

                ImGui::Text( "Rot: X: %.3f, Y: %.3f, Z: %.3f", angles.m_x.ToDegrees().ToFloat(), angles.m_y.ToDegrees().ToFloat(), angles.m_z.ToDegrees().ToFloat() );
                ImGui::Text( "Trans: X: %.3f, Y: %.3f, Z: %.3f", translation.m_x, translation.m_y, translation.m_z );
                ImGui::Text( "Scl: %.3f", transform.GetScale() );
            }
        }
        else
        {
            ImGui::Text( "Not Set" );
        }
    }

    static void TraverseHierarchy( VisualGraph::BaseNode const* pNode, TVector<VisualGraph::BaseNode const*>& nodePath )
    {
        EE_ASSERT( pNode != nullptr );
        nodePath.emplace_back( pNode );

        if ( pNode->HasParentGraph() && !pNode->GetParentGraph()->IsRootGraph() )
        {
            TraverseHierarchy( pNode->GetParentGraph()->GetParentNode(), nodePath );
        }
    }

    //-------------------------------------------------------------------------

    void FlowToolsNode::DrawExtraControls( VisualGraph::DrawContext const& ctx, VisualGraph::UserContext* pUserContext )
    {
        auto pGraphNodeContext = static_cast<ToolsGraphUserContext*>( pUserContext );
        bool const isPreviewing = pGraphNodeContext->HasDebugData();
        int16_t const runtimeNodeIdx = isPreviewing ? pGraphNodeContext->GetRuntimeGraphNodeIndex( GetID() ) : InvalidIndex;
        bool const isPreviewingAndValidRuntimeNodeIdx = isPreviewing && ( runtimeNodeIdx != InvalidIndex );

        //-------------------------------------------------------------------------
        // Draw Pose Node
        //-------------------------------------------------------------------------

        if ( GetValueType() == GraphValueType::Pose )
        {
            if ( isPreviewingAndValidRuntimeNodeIdx && pGraphNodeContext->IsNodeActive( runtimeNodeIdx ) )
            {
                PoseNodeDebugInfo const debugInfo = pGraphNodeContext->GetPoseNodeDebugInfo( runtimeNodeIdx );
                DrawPoseNodeDebugInfo( ctx, GetWidth(), debugInfo );
            }
            else
            {
                DrawEmptyPoseNodeDebugInfo( ctx, GetWidth() );
            }

            ImGui::SetCursorPosY( ImGui::GetCursorPosY() + 4 );

            DrawInfoText( ctx );
        }

        //-------------------------------------------------------------------------
        // Draw Value Node
        //-------------------------------------------------------------------------

        else
        {
            DrawInfoText( ctx );

            if ( GetValueType() != GraphValueType::Unknown && GetValueType() != GraphValueType::BoneMask && GetValueType() != GraphValueType::Pose )
            {
                BeginDrawInternalRegion( ctx, Color( 40, 40, 40 ), 4 );

                if ( isPreviewingAndValidRuntimeNodeIdx && pGraphNodeContext->IsNodeActive( runtimeNodeIdx ) )
                {
                    if ( HasOutputPin() )
                    {
                        GraphValueType const valueType = GetValueType();
                        switch ( valueType )
                        {
                            case GraphValueType::Bool:
                            {
                                auto const value = pGraphNodeContext->GetRuntimeNodeDebugValue<bool>( runtimeNodeIdx );
                                ImGui::Text( value ? "Value: True" : "Value: False" );
                            }
                            break;

                            case GraphValueType::ID:
                            {
                                auto const value = pGraphNodeContext->GetRuntimeNodeDebugValue<StringID>( runtimeNodeIdx );
                                if ( value.IsValid() )
                                {
                                    ImGui::Text( "Value: %s", value.c_str() );
                                }
                                else
                                {
                                    ImGui::Text( "Value: Invalid" );
                                }
                            }
                            break;

                            case GraphValueType::Int:
                            {
                                auto const value = pGraphNodeContext->GetRuntimeNodeDebugValue<int32_t>( runtimeNodeIdx );
                                ImGui::Text( "Value: %d", value );
                            }
                            break;

                            case GraphValueType::Float:
                            {
                                auto const value = pGraphNodeContext->GetRuntimeNodeDebugValue<float>( runtimeNodeIdx );
                                ImGui::Text( "Value: %.3f", value );
                            }
                            break;

                            case GraphValueType::Vector:
                            {
                                auto const value = pGraphNodeContext->GetRuntimeNodeDebugValue<Vector>( runtimeNodeIdx );
                                DrawVectorInfoText( ctx, value );
                            }
                            break;

                            case GraphValueType::Target:
                            {
                                auto const value = pGraphNodeContext->GetRuntimeNodeDebugValue<Target>( runtimeNodeIdx );
                                DrawTargetInfoText( ctx, value );
                            }
                            break;

                            case GraphValueType::BoneMask:
                            case GraphValueType::Pose:
                            default:
                            break;
                        }
                    }
                }
                else
                {
                    ImGui::NewLine();
                }

                EndDrawInternalRegion( ctx );
            }
        }
    }

    bool FlowToolsNode::IsActive( VisualGraph::UserContext* pUserContext ) const
    {
        auto pGraphNodeContext = static_cast<ToolsGraphUserContext*>( pUserContext );
        if ( pGraphNodeContext->HasDebugData() )
        {
            // Some nodes dont have runtime representations
            auto const runtimeNodeIdx = pGraphNodeContext->GetRuntimeGraphNodeIndex( GetID() );
            if ( runtimeNodeIdx != InvalidIndex )
            {
                return pGraphNodeContext->IsNodeActive( runtimeNodeIdx );
            }
        }

        return false;
    }

    ImColor FlowToolsNode::GetTitleBarColor() const
    {
        return ImGuiX::ToIm( GetColorForValueType( GetValueType() ) );
    }

    ImColor FlowToolsNode::GetPinColor( VisualGraph::Flow::Pin const& pin ) const
    {
        return ImGuiX::ToIm( GetColorForValueType( (GraphValueType) pin.m_type ) );
    }

    void FlowToolsNode::DrawContextMenuOptions( VisualGraph::DrawContext const& ctx, VisualGraph::UserContext* pUserContext, Float2 const& mouseCanvasPos, VisualGraph::Flow::Pin* pPin )
    {
        if ( ImGui::BeginMenu( EE_ICON_INFORMATION_OUTLINE" Node Info" ) )
        {
            // UUID
            auto IDStr = GetID().ToString();
            InlineString label = InlineString( InlineString::CtorSprintf(), "UUID: %s", IDStr.c_str() );
            if ( ImGui::MenuItem( label.c_str() ) )
            {
                ImGui::SetClipboardText( IDStr.c_str() );
            }

            // Draw runtime node index
            auto pGraphNodeContext = static_cast<ToolsGraphUserContext*>( pUserContext );
            if ( pGraphNodeContext->HasDebugData() )
            {
                int16_t runtimeNodeIdx = pGraphNodeContext->GetRuntimeGraphNodeIndex( GetID() );
                if ( runtimeNodeIdx != InvalidIndex )
                {
                    label = InlineString( InlineString::CtorSprintf(), "Runtime Index: %d", runtimeNodeIdx );
                    if ( ImGui::MenuItem( label.c_str() ) )
                    {
                        InlineString const value( InlineString::CtorSprintf(), "%d", runtimeNodeIdx );
                        ImGui::SetClipboardText( value.c_str() );
                    }
                }
            }

            ImGui::EndMenu();
        }
    }
}