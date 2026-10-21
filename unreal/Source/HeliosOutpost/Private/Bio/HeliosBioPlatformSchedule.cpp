#include "Bio/HeliosBioPlatformSchedule.h"

namespace HeliosBioPlatformSchedule
{
bool ShouldUsePlatform(float Ground,float Platform)
{
    return FMath::IsFinite(Platform)&&Platform>=0.f&&
        (Ground<0.f||(Platform<=Ground*.8f&&Ground-Platform>2.f));
}
float CycleSeconds(const FSnapshot& Route)
{
    if(Route.Points.Num()<2||!FMath::IsFinite(Route.Speed)||Route.Speed<=0.f)return 0.f;
    float Length=0.f;
    for(int32 I=1;I<Route.Points.Num();++I)Length+=FVector::Dist(Route.Points[I-1],Route.Points[I]);
    if(Route.Replay==EHeliosBioPlatformReplay::PingPong)return 2.f*Length/Route.Speed+2.f*Route.Pause;
    if(Route.Replay==EHeliosBioPlatformReplay::Wrap)return (Length+FVector::Dist(Route.Points.Last(),Route.Points[0]))/Route.Speed+Route.Pause;
    return Length/Route.Speed;
}
bool Predict(const FSnapshot& Route,int32 BoardIndex,int32 ExitIndex,float Earliest,float& Board,float& Exit)
{
    Board=Exit=-1.f;
    if(!Route.bRunning||!Route.Points.IsValidIndex(BoardIndex)||
        (ExitIndex!=INDEX_NONE&&!Route.Points.IsValidIndex(ExitIndex))||
        !Route.Points.IsValidIndex(Route.TargetIndex)||!FMath::IsFinite(Earliest)||Earliest<0.f||
        !FMath::IsFinite(Route.Speed)||Route.Speed<=0.f||Route.Pause<0.f)return false;
    const float Cycle=CycleSeconds(Route);
    if(Cycle<=KINDA_SMALL_NUMBER)return false;
    FVector Position=Route.Position;
    int32 Target=Route.TargetIndex,Direction=Route.Direction;
    float Now=0.f,Wait=Route.Wait;
    auto Visit=[&](int32 Index,float Dwell)
    {
        if(Board<0.f&&Index==BoardIndex&&Earliest<=Now+Dwell+KINDA_SMALL_NUMBER)Board=FMath::Max(Now,Earliest);
        if(Board>=0.f&&(ExitIndex==INDEX_NONE||Index==ExitIndex)){Exit=FMath::Max(Now,Board);return true;}
        return false;
    };
    // Include a dock occupied now, including the remainder of its endpoint pause.
    for(int32 I=0;I<Route.Points.Num();++I)
        if(Position.Equals(Route.Points[I],.1f)&&Visit(I,Wait))return true;
    // At most enough cycles to reach the approach time, then board and disembark.
    const float Horizon=Earliest+2.f*Cycle+1.f;
    const int32 Limit=FMath::Min(4096,Route.Points.Num()*(FMath::CeilToInt(Earliest/Cycle)+4)*2);
    for(int32 Step=0;Step<Limit&&Now<=Horizon;++Step)
    {
        Now+=Wait;Wait=0.f;
        Now+=FVector::Dist(Position,Route.Points[Target])/Route.Speed;Position=Route.Points[Target];
        const int32 Arrived=Target,Next=Target+Direction;
        bool Running=true;
        if(Route.Points.IsValidIndex(Next))Target=Next;
        else
        {
            Wait=Route.Pause;
            if(Route.Replay==EHeliosBioPlatformReplay::PingPong){Direction=-Direction;Target+=Direction;}
            else if(Route.Replay==EHeliosBioPlatformReplay::Wrap){Target=0;Direction=1;}
            else Running=false;
        }
        if(Visit(Arrived,Running?Wait:0.f))return true;
        if(!Running)break;
    }
    return false;
}
}
