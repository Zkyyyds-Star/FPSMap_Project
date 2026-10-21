#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "Bio/HeliosBioRules.h"
#include "Bio/HeliosBioPlatformTraversal.h"
#include "AI/Navigation/NavigationTypes.h"
#include "HeliosBioHotelQA.generated.h"

UCLASS()
class HELIOSOUTPOST_API AHeliosBioHotelQA : public AActor
{
    GENERATED_BODY()
public:
    AHeliosBioHotelQA();
    virtual void Tick(float Dt) override;
private:
    void Check(const TCHAR* Name,bool Passed);
    void Capture(const TCHAR* Name);
    void RecordPresentation(const TCHAR* Name);
    void Finish();
    void TickNaturalRound(float Dt,double Now);
    void SampleNaturalRound(double Now);
    void FinishNaturalRound(bool Completed);
    void SampleNPCDiagnostics(class AHeliosBioNPC* NPC,const struct FHeliosBioParticipant& State,const TSharedPtr<class FJsonObject>& Row,double Now);
    void SendNaturalKey(FKey Key,bool Down);
    void ReleaseNaturalKeys();
    class AHeliosSSXEffect* FindSupplyEffect(class AHeliosBioSupply* Supply) const;
    void RecordSupplyEffects(const TCHAR* Phase);
    void RecordPlatformDiagnostics(const TCHAR* Phase);
    void TickHunterChoice(double Now);
    void TickPlatformPhysics(double Now);
    void TickFloatingAI(float Dt,double Now);
    void FinishFloatingAI(const FString& Blocker=FString());
    TWeakObjectPtr<class AHeliosBioNPC> TacticsOpponent,TacticsSecond;
    HeliosBioPlatformTraversal::FTraversalRequest TacticsRequest;
    FVector TacticsA=FVector::ZeroVector,TacticsB=FVector::ZeroVector,TacticsMiddle=FVector::ZeroVector;
    float TacticsDistance=0.f;
    double TacticsAt=0;
    UPROPERTY() TObjectPtr<AActor> TacticsBlocker;
    void TickLegacyPlatformPhysics(double Now);
    void TickPlatformRoute(double Now);
    bool PlanPlatformRoute(class ACharacter* Character,bool ForAI);
    bool SetPlatformWalkPath(class ACharacter* Character,const FVector& Goal);
    bool AdvancePlatformWalkPath(class ACharacter* Character);
    void SamplePlatformRoute(double Now);
    void FinishSpecial(const TCHAR* Domain,const FString& Blocker=FString());
    int32 ZombieId() const;
    int32 Stage=0,Failures=0,ShotsBefore=0,AmmoBefore=0,Zombie=INDEX_NONE,SerialBefore=0;
    double Started=0,Next=0;
    float HealthBefore=0;
    bool bLiveFireEvidenceRecorded=false;
    FString Out;
    FString ExpectedMap=TEXT("/Game/Helios/Maps/CFMBioPyramid");
    int32 SoakConfiguredCount=0;
    float SoakConfiguredPreparation=0,SoakConfiguredRound=0;
    FVector PlayerBefore=FVector::ZeroVector;
    TArray<FVector> BotBefore;
    TArray<int32> BotTeamsBefore;
    int32 MeleeBefore=0;
    int32 AreaHitsBefore=0;
    float CloneMotherMaxHPAtSpawn=0;
    int32 GenericFailures=-1;
    bool bModernRoleFixtureActive=false,bBlueSupplySourceVerified=false,bBlueUltimateIdentityVerified=false,bBlueUltimateCombatVerified=false;
    FString BlueUltimateIdentity,BlueSupplyMesh;
    bool bGenericUltimateDeathObserved=false,bGenericUltimateResetObserved=false,bBlueEffectLifecycleObserved=false;
    UPROPERTY() TObjectPtr<class AHeliosBioSupply> BlueFixtureSupply;
    TWeakObjectPtr<class AHeliosSSXEffect> BlueFixtureEffect;
    TArray<TSharedPtr<class FJsonValue>> SupplyEffectEvidence;
    TArray<TSharedPtr<class FJsonValue>> PlatformDiagnostics;
    FName ChoiceRole=NAME_None;
    bool bChoiceSelected=false,bChoiceSequenceCompleted=false,bPlatformCarried=false,bPlatformRayBlocked=false;
    int32 ChoiceLastStage=-1;
    TArray<TSharedPtr<class FJsonValue>> HunterCandidateEvidence;
    UPROPERTY() TObjectPtr<class UHeliosBioFourSlashComponent> ChoiceSequence;
    UPROPERTY() TObjectPtr<class AHeliosBioMovingPlatform> PhysicsPlatform;
    FVector PlatformBefore=FVector::ZeroVector,PlatformPlayerBefore=FVector::ZeroVector,PlatformRelativeBefore=FVector::ZeroVector;
    UPROPERTY() TObjectPtr<class AHeliosBioNPC> AIBoardBot;
    bool bAIBoardLaunchObserved=false,bAIBoardLanded=false,bAIBoardCarried=false;
    double AIBoardDeadline=0;
    FVector AIBoardStart=FVector::ZeroVector,AIBoardRelative=FVector::ZeroVector;
    TArray<TSharedPtr<class FJsonValue>> AIBoardEvidence;
    TArray<TSharedPtr<class FJsonValue>> PlatformRouteEvidence;
    TArray<FNavPathPoint> PlatformWalkPath;
    struct FPlatformWalkJumpSegment
    {
        TWeakObjectPtr<class AHeliosBioStaticJumpLink> Link;
        FVector Destination=FVector::ZeroVector;
        int32 EndIndex=INDEX_NONE;
        FString Match;
    };
    TMap<int32,FPlatformWalkJumpSegment> PlatformWalkJumps;
    TWeakObjectPtr<class AHeliosBioStaticJumpLink> RouteWalkJumpLink;
    int32 RouteWalkJumpEndIndex=INDEX_NONE,RouteWalkJumpLandingsBefore=0,RouteWalkJumpFailuresBefore=0;
    int32 RouteStaticJumpAttemptsBefore=0,RouteStaticJumpLandingsBefore=0,RouteStaticJumpFailuresBefore=0;
    int32 PlayerStaticJumpAttempts=0,PlayerStaticJumpLandings=0,PlayerStaticJumpFailures=0,AIStaticJumpAttempts=0,AIStaticJumpLandings=0,AIStaticJumpFailures=0;
    bool bRouteWalkJumpPending=false,bPlatformPathMetadataAvailable=false;
    FString PlatformWalkBlocker;
    FVector RouteEntry=FVector::ZeroVector,RouteExit=FVector::ZeroVector,RouteGoal=FVector::ZeroVector,RouteReturnGoal=FVector::ZeroVector,RouteSpawn=FVector::ZeroVector,RouteLastPosition=FVector::ZeroVector,RouteRideRelative=FVector::ZeroVector,RouteRidePlatform=FVector::ZeroVector,RouteRideActor=FVector::ZeroVector;
    int32 RouteState=0,RouteLeg=0,RoutePathIndex=0,PlayerRouteLegs=0,AIRouteLegs=0,RouteBoardingsBefore=0,RouteDeparturesBefore=0,PlatformProjectileImpacts=0,RouteProjectileImpactsBefore=0,RouteTargetRepositions=0;
    TWeakObjectPtr<class AHeliosChanZeusWeapon> RouteNativeZeus;
    int32 PlatformNativeImpacts=0,RouteNativeImpactsBefore=0,RouteNativeShotsBefore=0,PlatformNativeLastShotSerial=0,PlatformProjectileLastPendingPlayerShot=0;
    FString PlatformWeaponEvidenceBlocker;
    bool PlacePlatformOpponentFixture(class ACharacter* TestedActor,const TCHAR* StageName);
    bool PlacePlatformAboardOpponentFixture(class ACharacter* TestedActor,const TCHAR* StageName);
    bool bRouteTargetPlatformLandingPending=false,bRouteTargetAboardFixture=false;
    double RouteTargetPlatformPlacedAt=0;
    int32 RouteTargetPlatformLandings=0;
    double RouteDepartureWaitAt=0;
    TArray<FVector> RouteFixtureEntryShoreCandidates,RouteFixtureExitShoreCandidates;
    float RouteWalkingDistance=0.f,RoutePostExitDistance=0.f;
    double RouteStateAt=0,RouteLegAt=0,RouteSampleAt=0,RouteLastAt=0,RouteStillSeconds=0,RouteWeaponAt=0;
    bool bPlatformRoutePlayer=true,bPlayerRouteCompleted=false,bAIRouteCompleted=false,bRouteWait=false,bRouteJump=false,bRouteCarry=false,bRouteExitJump=false,bRouteAIResume=false,bProductionPlatformWeaponBlocked=false,bRouteWeaponProbe=false,bRouteWeaponAllBlocked=false;
    FString PlayerRouteBlocker,AIRouteBlocker;
    double SoakNextSample=0,SoakActiveWall=0,SoakWinnerWall=0;
    float SoakActiveGame=0,SoakActiveDuration=0,SoakRemainingAtWinner=0;
    int32 SoakWinner=0,SoakSamples=0,SoakNavQueries=0,SoakNavSuccess=0,SoakGrounded=0,SoakSerial=0,SoakInfections=0,SoakHumanShots=0,SoakZombieMelee=0;
    bool bSoakHumanMoved=false,bSoakZombieMoved=false,bSoakCombatImage=false,bSoakRestarted=false;
    bool bObserveOnly=false,bInputEvents=false,bInputFireHeld=false,bInputMoveHeld=false,bInputUIRequested=false,bInputPauseVerified=false,bInputResumeVerified=false;
    double InputUIAt=0,InputPulseUntil=0;
    int32 SoakSeed=1337,InputEventsSent=0;
    float ObserverSeconds=480.f;
    int32 InputTestStage=0;
    double InputTestAt=0;
    FVector InputSmokeBefore=FVector::ZeroVector;
    FKey NaturalMoveKey;
    bool bInputMovementVerified=false,bInputJumpVerified=false;
    struct FActorNaturalEvidence
    {
        double LastSample=0;
        FVector LastPosition=FVector::ZeroVector;
        FString Clip;
        float ClipTime=0,AnimationStall=0,MovementStall=0,FootGapAge=0;
        float LongestAnimationStall=0,LongestMovementStall=0,MaxAbsoluteFootGap=0;
        TArray<FTransform> Pose;
        int32 ClockAdvances=0,PoseAdvances=0,GroundSamples=0,MotionSamples=0;
        bool bFootFailed=false,bAnimationFailed=false,bMovementFailed=false;
    };
    TMap<int32,FActorNaturalEvidence> NaturalActorEvidence;
    TMap<int32,EHeliosBioTeam> SoakPreviousTeams;
    TSet<FKey> HeldNaturalKeys;
    TArray<TSharedPtr<class FJsonValue>> NaturalInputEvidence;
    TMap<int32,FVector> SoakPreviousPositions;
    TMap<int32,int32> SoakPreviousShots,SoakPreviousMelee;
    FVector AreaVictimBefore=FVector::ZeroVector;
    UPROPERTY() TObjectPtr<class AHeliosBioClone> CloneFirst;
    UPROPERTY() TObjectPtr<class AHeliosBioClone> CloneSecond;
    TArray<TSharedPtr<class FJsonValue>> Checks;
    TArray<TSharedPtr<class FJsonValue>> PresentationEvidence;
    UPROPERTY() TObjectPtr<class AHeliosBioHotelGameMode> Mode;
};
