#pragma once
#include "System/Time/Timers.h"
#include "System/Math/Vector.h"

//-------------------------------------------------------------------------

namespace EE::AI
{
    struct BehaviorContext;

    //-------------------------------------------------------------------------

    // IDLE 行为
    class IdleAction
    {
    public:

        void Start( BehaviorContext const& ctx );
        void Update( BehaviorContext const& ctx );

    private:

        void ResetIdleBreakerTimer();

    private:

        ManualCountdownTimer        m_idleBreakerCooldown;
    };
}