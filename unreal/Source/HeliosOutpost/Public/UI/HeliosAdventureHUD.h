#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Player/HeliosLoadout.h"
#include "HeliosAdventureHUD.generated.h"

class AHeliosAdventureDirector;
class AHeliosEnemyBoss;
class SWidget;
class UAudioComponent;
class USoundBase;
enum class EHeliosPetKind : uint8;
enum class EHeliosYunSummerForm : uint8;

enum class EHeliosAdventureMenu : uint8
{
    None,
    Pause,
    RestartConfirm,
    Death,
    WeaponSelect,
    TownMap
};

// Presentation-only snapshot; gameplay remains owned by the character and pet subsystem.
struct FHeliosStatusPanelData
{
    float Health = 0.f, MaxHealth = 0.f;
    FText WeaponName, ResourceText;
    bool bHasPlayer = false, bReloading = false, bAutonomy = false;
    bool bHasPet = false, bPetSleeping = false;
    FText PetName, PetActivity;
    float PetHealth = 0.f, PetMaxHealth = 0.f, PetDistanceMetres = 0.f;
    bool bHasPetDistance = false;
    int32 PetCount=0;
    FText PetRosterStatus;
    static float Fraction(float Current, float Maximum)
    { return Maximum > 0.f ? FMath::Clamp(Current / Maximum, 0.f, 1.f) : 0.f; }
};

UCLASS()
class HELIOSOUTPOST_API AHeliosAdventureHUD : public AHUD
{
    GENERATED_BODY()

public:
    AHeliosAdventureHUD();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void DrawHUD() override;

    void TogglePauseMenu();
    void RequestRestartConfirmation();
    void CancelRestartConfirmation();
    void ContinueRun();
    void ConfirmRestart();
    void QuitToDesktop();
    void SelectWeapon(FName WeaponId);
    void SelectCharacter(FName CharacterId, FName VariantId=NAME_None);
    void SelectLoadout(const FHeliosLoadout& Loadout);
    FHeliosLoadout GetPendingLoadout() const { return PendingLoadout; }
    FName GetPendingCharacterId() const { return PendingLoadout.CharacterId; }
    FText GetSelectedCharacterName() const;
    FName GetSelectedCharacterVariant(bool bPet=false) const;
    void SelectCharacterForm(FName VariantId,bool bPet=false);
    FHeliosLoadout GetPetLoadout(int32 Slot) const;
    void SelectPetCharacter(FName CharacterId, FName VariantId=NAME_None);
    void SelectPetWeapon(FName WeaponId);
    void SelectPet(EHeliosPetKind Kind);
    void SelectPetCount(int32 Count);
    void SelectPetSlot(int32 Slot);
    int32 GetSelectedPetCount() const;
    int32 GetActivePetSlot() const { return ActivePetSlot; }
    EHeliosPetKind GetPetKind(int32 Slot) const;
    FString GetPetRosterSummary() const;
    EHeliosPetKind GetSelectedPet() const;
    EHeliosYunSummerForm GetYunSummerForm(bool bPet) const;
    void SelectYunSummerForm(EHeliosYunSummerForm Form, bool bPet);
    FText GetSelectedEquipmentName() const;
    void StartSelectedRun();
    void ShowDeploymentFailure(const FString& Reason);
    bool IsFighterHUD() const;
    void SelectMap(FName MapId);
    FName GetPendingMapId() const { return PendingMapId; }
    bool IsTownWorld() const;
    bool IsPendingTownMap() const;
    FName GetCurrentMapAsset() const;
    void ToggleTownMap();
    void InteractTown();
    void RequestNewTown();
    FText GetTownInteractionText() const;
    FText GetControlsText() const;
    FName GetPendingWeaponId() const { return PendingLoadout.PrimaryWeaponId; }

    EHeliosAdventureMenu GetMenuState() const { return MenuState; }
    FText GetZoneText() const;
    FText GetRunText() const;
    FText GetVitalsText() const;
    FHeliosStatusPanelData GetStatusPanelData() const;
    FText GetMenuTitle() const;
    FText GetMenuDetails() const;
    FText GetBossTitleText() const;
    FText GetBossMetaText() const;
    FText GetIkunAlertText() const;
    float GetBossHealthFraction() const;
    float GetBossDisplayFraction() const { return BossDisplayHP; }
    float GetBossTrailFraction() const { return BossTrailHP; }
    float GetBossBarOpacity() const { return BossBarOpacity; }
    float GetBossHitFlash() const { return BossHitFlash; }
    FString GetBossBarTitle() const { return BossBarTitle; }
    FText GetBossAttackText() const;
    float GetBossWindupFraction() const;
    bool HasActiveBoss() const;
    bool IsIkunAlertVisible() const;
    bool IsMemeSoundEnabled() const { return bMemeSoundEnabled; }
    void ToggleMemeSound();
    bool IsUrbanSkipVisible() const;
    bool CanSkipUrbanWave() const;
    void BeginUrbanSkipClick();
    void FinishUrbanSkipClick();
    void ShowUrbanPointer();
    void HideUrbanPointer();

private:
    AHeliosAdventureDirector* GetDirector() const;
    void ApplyMenuInput(bool bOpen);
    void HandleRestartKey();
    void UpdateBossPresentation();
    AHeliosEnemyBoss* ChoosePrimaryBoss(const TArray<AHeliosEnemyBoss*>& Bosses) const;
    void PlayMemeSound(bool bEntrance);
    void StopMemeSound();
    double GetRealTimeSeconds() const;
    void ResetBossPresentation();
    void TickBossBar(float DeltaSeconds);
    float BossDisplayHP=1.f, BossTrailHP=1.f, BossLastHP=1.f;
    float BossBarOpacity=0.f, BossHitFlash=0.f, BossTrailDelay=0.f;
    int32 BossBarNumber=INDEX_NONE;
    FString BossBarTitle;

    EHeliosAdventureMenu MenuState = EHeliosAdventureMenu::None;
    EHeliosAdventureMenu MenuBeforeConfirm = EHeliosAdventureMenu::None;
    FHeliosLoadout PendingLoadout;
    FName PendingMapId = TEXT("HeliosAdventure");
    int32 ActivePetSlot=0;
    bool bNewTownConfirm = false;
    bool bTravelRequested = false;
    FString SelectionError;
    TSharedPtr<SWidget> RootWidget;

    UPROPERTY(Transient) TObjectPtr<AHeliosEnemyBoss> PrimaryBoss;
    UPROPERTY(Transient) TObjectPtr<USoundBase> EntranceSound;
    UPROPERTY(Transient) TObjectPtr<USoundBase> ReactSound;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> MemeAudioComponent;
    int32 LastBossSpawnSerial = INDEX_NONE;
    int32 LastBossKills = 0;
    int32 LastBossPhase = 0;
    double LastMemeSoundSeconds = -1000.0;
    double IkunAlertUntilSeconds = 0.0;
    FString IkunAlert;
    bool bMemeSoundEnabled = true;
    bool bUrbanPointer = false;
    bool bUrbanPointerBound = false;
    int32 UrbanClickRun = INDEX_NONE, UrbanClickWave = INDEX_NONE;
};
