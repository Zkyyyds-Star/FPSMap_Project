#include "HeliosEnemyStateTreeTools.h"

#include "Components/StateTreeAIComponentSchema.h"
#include "Components/StateTreeComponentSchema.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorNode.h"
#include "StateTreeEditorPropertyBindings.h"
#include "StateTreeState.h"
#include "StateTreeSchema.h"
#include "StateTreeTaskBase.h"
#include "UObject/UnrealType.h"
#include "PropertyBindingPath.h"

namespace
{
FString ClassPath(const UClass* Class)
{
    return Class ? Class->GetPathName() : FString();
}

UClass* ReadClassProperty(const UObject* Object, const FName PropertyName)
{
    if (!Object)
    {
        return nullptr;
    }

    const FClassProperty* Property = FindFProperty<FClassProperty>(Object->GetClass(), PropertyName);
    return Property ? Cast<UClass>(Property->GetObjectPropertyValue_InContainer(Object)) : nullptr;
}

void DescribeSchema(const UStateTreeSchema* Schema, const FString& Prefix, const TSharedRef<FJsonObject>& Result)
{
    Result->SetStringField(Prefix + TEXT("_schema_class"), Schema ? Schema->GetClass()->GetPathName() : FString());

    const UStateTreeComponentSchema* ComponentSchema = Cast<UStateTreeComponentSchema>(Schema);
    Result->SetStringField(
        Prefix + TEXT("_context_actor_class"),
        ComponentSchema ? ClassPath(ComponentSchema->GetContextActorClass()) : FString());
    Result->SetStringField(
        Prefix + TEXT("_ai_controller_class"),
        ClassPath(ReadClassProperty(Schema, TEXT("AIControllerClass"))));
}

int32 DisableDebugNodesInState(UStateTreeState* State)
{
    if (!State)
    {
        return 0;
    }

    int32 DisabledCount = 0;
    const auto DisableNode = [&DisabledCount](FStateTreeEditorNode& EditorNode)
    {
        const UScriptStruct* NodeType = EditorNode.Node.GetScriptStruct();
        if (NodeType && NodeType->GetFName() == TEXT("StateTreeDebugTextTask"))
        {
            FStateTreeTaskBase* Task = reinterpret_cast<FStateTreeTaskBase*>(EditorNode.Node.GetMutableMemory());
            if (Task && Task->bTaskEnabled)
            {
                Task->bTaskEnabled = false;
                ++DisabledCount;
            }
        }
    };

    for (FStateTreeEditorNode& Task : State->Tasks)
    {
        DisableNode(Task);
    }
    DisableNode(State->SingleTask);
    for (UStateTreeState* Child : State->Children)
    {
        DisabledCount += DisableDebugNodesInState(Child);
    }
    return DisabledCount;
}

void CollectStateNodes(UStateTreeState* State, TArray<FStateTreeEditorNode*>& OutNodes)
{
    if (!State)
    {
        return;
    }

    for (FStateTreeEditorNode& Node : State->EnterConditions)
    {
        OutNodes.Add(&Node);
    }
    for (FStateTreeEditorNode& Node : State->Tasks)
    {
        OutNodes.Add(&Node);
    }
    if (State->SingleTask.Node.IsValid())
    {
        OutNodes.Add(&State->SingleTask);
    }
    for (FStateTreeTransition& Transition : State->Transitions)
    {
        for (FStateTreeEditorNode& Node : Transition.Conditions)
        {
            OutNodes.Add(&Node);
        }
    }
    for (UStateTreeState* Child : State->Children)
    {
        CollectStateNodes(Child, OutNodes);
    }
}

void CollectStates(UStateTreeState* State, TArray<UStateTreeState*>& OutStates)
{
    if (!State)
    {
        return;
    }
    OutStates.Add(State);
    for (UStateTreeState* Child : State->Children)
    {
        CollectStates(Child, OutStates);
    }
}

FString GuidString(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}
}

int32 UHeliosEnemyStateTreeTools::DisableDebugTextTasks(UStateTree* StateTree)
{
    if (!StateTree)
    {
        return 0;
    }

    UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData);
    if (!EditorData)
    {
        return 0;
    }

    int32 DisabledCount = 0;
    for (UStateTreeState* RootState : EditorData->SubTrees)
    {
        DisabledCount += DisableDebugNodesInState(RootState);
    }
    for (FStateTreeEditorNode& GlobalTask : EditorData->GlobalTasks)
    {
        const UScriptStruct* NodeType = GlobalTask.Node.GetScriptStruct();
        if (NodeType && NodeType->GetFName() == TEXT("StateTreeDebugTextTask"))
        {
            FStateTreeTaskBase* Task = reinterpret_cast<FStateTreeTaskBase*>(GlobalTask.Node.GetMutableMemory());
            if (Task && Task->bTaskEnabled)
            {
                Task->bTaskEnabled = false;
                ++DisabledCount;
            }
        }
    }

    if (DisabledCount > 0)
    {
        StateTree->Modify();
        UStateTreeEditingSubsystem::MarkAsPubliclyModified(StateTree);
        StateTree->MarkPackageDirty();
    }
    return DisabledCount;
}

FString UHeliosEnemyStateTreeTools::RepairShooterTargetBindings(UStateTree* StateTree)
{
    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset"), GetPathNameSafe(StateTree));
    UStateTreeEditorData* EditorData = StateTree ? Cast<UStateTreeEditorData>(StateTree->EditorData) : nullptr;
    if (!EditorData)
    {
        Result->SetBoolField(TEXT("success"), false);
        Result->SetStringField(TEXT("error"), TEXT("missing_editor_data"));
    }
    else
    {
        TArray<FStateTreeEditorNode*> Nodes;
        for (FStateTreeEditorNode& GlobalTask : EditorData->GlobalTasks)
        {
            Nodes.Add(&GlobalTask);
        }
        TArray<UStateTreeState*> States;
        for (UStateTreeState* RootState : EditorData->SubTrees)
        {
            CollectStates(RootState, States);
            CollectStateNodes(RootState, Nodes);
        }

        TArray<TSharedPtr<FJsonValue>> BeforeBindings;
        for (const FStateTreePropertyPathBinding& Binding : EditorData->EditorBindings.GetBindings())
        {
            TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("source_id"), GuidString(Binding.GetSourcePath().GetStructID()));
            Entry->SetStringField(TEXT("source_path"), Binding.GetSourcePath().ToString());
            Entry->SetStringField(TEXT("target_id"), GuidString(Binding.GetTargetPath().GetStructID()));
            Entry->SetStringField(TEXT("target_path"), Binding.GetTargetPath().ToString());
            BeforeBindings.Add(MakeShared<FJsonValueObject>(Entry));
        }
        Result->SetArrayField(TEXT("bindings_before"), BeforeBindings);

        const bool bLinkedShootTree = StateTree->GetPathName().Contains(TEXT("ST_Shooter_ShootAtTarget"));
        FGuid CanonicalSourceID;
        FString CanonicalSourcePath;
        if (bLinkedShootTree)
        {
            CanonicalSourceID = EditorData->GetRootParametersGuid();
            CanonicalSourcePath = TEXT("Target");
        }
        else
        {
            for (FStateTreeEditorNode* Node : Nodes)
            {
                const UScriptStruct* Type = Node ? Node->Node.GetScriptStruct() : nullptr;
                if (Type && Type->GetFName() == FName(TEXT("StateTreeSenseEnemiesTask")))
                {
                    CanonicalSourceID = Node->ID;
                    CanonicalSourcePath = TEXT("TargetActor");
                    break;
                }
            }
        }

        int32 RepairedCount = 0;
        TArray<TSharedPtr<FJsonValue>> Repairs;
        const auto AddBinding = [&](const FGuid& TargetID, const FString& TargetPath, const FString& TargetLabel)
        {
            if (!CanonicalSourceID.IsValid() || !TargetID.IsValid())
            {
                return;
            }
            FPropertyBindingPath Source(CanonicalSourceID);
            FPropertyBindingPath Target(TargetID);
            if (!Source.FromString(CanonicalSourcePath) || !Target.FromString(TargetPath))
            {
                return;
            }
            EditorData->AddPropertyBinding(Source, Target);
            TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("source_id"), GuidString(CanonicalSourceID));
            Entry->SetStringField(TEXT("source_path"), CanonicalSourcePath);
            Entry->SetStringField(TEXT("target_id"), GuidString(TargetID));
            Entry->SetStringField(TEXT("target_path"), TargetPath);
            Entry->SetStringField(TEXT("target_node"), TargetLabel);
            Repairs.Add(MakeShared<FJsonValueObject>(Entry));
            ++RepairedCount;
        };

        for (FStateTreeEditorNode* Node : Nodes)
        {
            const UScriptStruct* Type = Node ? Node->Node.GetScriptStruct() : nullptr;
            if (!Type)
            {
                continue;
            }
            const FName TypeName = Type->GetFName();
            if (TypeName == FName(TEXT("StateTreeFaceActorTask")))
            {
                AddBinding(Node->ID, TEXT("ActorToFaceTowards"), Type->GetName());
            }
            else if (TypeName == FName(TEXT("StateTreeLineOfSightToTargetCondition"))
                || TypeName == FName(TEXT("StateTreeShootAtTargetTask")))
            {
                AddBinding(Node->ID, TEXT("Target"), Type->GetName());
            }
        }

        if (!bLinkedShootTree)
        {
            for (UStateTreeState* State : States)
            {
                if (State && IsValid(State->LinkedAsset)
                    && State->LinkedAsset->GetPathName().Contains(TEXT("ST_Shooter_ShootAtTarget")))
                {
                    State->UpdateParametersFromLinkedSubtree();
                    AddBinding(State->Parameters.ID, TEXT("Target"),
                        FString::Printf(TEXT("LinkedAssetState:%s"), *State->GetPath()));
                }
            }
        }

        Result->SetStringField(TEXT("canonical_source_id"), GuidString(CanonicalSourceID));
        Result->SetStringField(TEXT("canonical_source_path"), CanonicalSourcePath);
        Result->SetNumberField(TEXT("repaired_count"), RepairedCount);
        Result->SetArrayField(TEXT("repairs"), Repairs);
        Result->SetBoolField(TEXT("success"), CanonicalSourceID.IsValid() && RepairedCount > 0);
        if (CanonicalSourceID.IsValid() && RepairedCount > 0)
        {
            StateTree->Modify();
            UStateTreeEditingSubsystem::MarkAsPubliclyModified(StateTree);
            StateTree->MarkPackageDirty();
        }
    }

    FString Output;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
    FJsonSerializer::Serialize(Result, Writer);
    return Output;
}

FString UHeliosEnemyStateTreeTools::CompileAndDescribe(UStateTree* StateTree)
{
    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    if (!StateTree)
    {
        Result->SetBoolField(TEXT("success"), false);
        Result->SetStringField(TEXT("error"), TEXT("null_state_tree"));
    }
    else
    {
        Result->SetStringField(TEXT("asset"), StateTree->GetPathName());

        UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData);
        const UStateTreeSchema* EditorSchema = EditorData ? EditorData->Schema : nullptr;
        DescribeSchema(EditorSchema, TEXT("editor"), Result);

        const uint32 HashBefore = StateTree->LastCompiledEditorDataHash;
        const uint32 ExpectedHash = UStateTreeEditingSubsystem::CalculateStateTreeHash(StateTree);
        Result->SetNumberField(TEXT("compiled_hash_before"), HashBefore);
        Result->SetNumberField(TEXT("editor_hash_before"), ExpectedHash);

        UStateTreeEditingSubsystem::MarkAsPubliclyModified(StateTree);
        Result->SetBoolField(TEXT("needed_recompile_after_mark"), UStateTreeEditingSubsystem::NeedsRecompile(StateTree));

        FStateTreeCompilerLog CompilerLog;
        const bool bCompiled = UStateTreeEditingSubsystem::CompileStateTree(StateTree, CompilerLog);
        CompilerLog.DumpToLog(StateTree, LogTemp);

        const uint32 HashAfter = StateTree->LastCompiledEditorDataHash;
        const uint32 CurrentEditorHash = UStateTreeEditingSubsystem::CalculateStateTreeHash(StateTree);
        Result->SetBoolField(TEXT("compiler_returned_success"), bCompiled);
        Result->SetNumberField(TEXT("compiled_hash_after"), HashAfter);
        Result->SetNumberField(TEXT("editor_hash_after"), CurrentEditorHash);
        Result->SetBoolField(TEXT("compiled_hash_nonzero"), HashAfter != 0);
        Result->SetBoolField(TEXT("compiled_hash_matches_editor"), HashAfter == CurrentEditorHash);
        Result->SetBoolField(TEXT("needs_recompile_after_compile"), UStateTreeEditingSubsystem::NeedsRecompile(StateTree));
        Result->SetBoolField(TEXT("ready_to_run"), StateTree->IsReadyToRun());
        DescribeSchema(StateTree->GetSchema(), TEXT("runtime"), Result);

        const bool bRuntimeMatchesEditor =
            EditorSchema != nullptr
            && StateTree->GetSchema() != nullptr
            && Result->GetStringField(TEXT("editor_context_actor_class")) == Result->GetStringField(TEXT("runtime_context_actor_class"))
            && Result->GetStringField(TEXT("editor_ai_controller_class")) == Result->GetStringField(TEXT("runtime_ai_controller_class"));
        Result->SetBoolField(TEXT("runtime_matches_editor"), bRuntimeMatchesEditor);
        Result->SetBoolField(
            TEXT("success"),
            bCompiled && HashAfter != 0 && HashAfter == CurrentEditorHash
            && !UStateTreeEditingSubsystem::NeedsRecompile(StateTree)
            && StateTree->IsReadyToRun() && bRuntimeMatchesEditor);

        if (bCompiled)
        {
            StateTree->MarkPackageDirty();
        }
    }

    FString Output;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
    FJsonSerializer::Serialize(Result, Writer);
    return Output;
}
