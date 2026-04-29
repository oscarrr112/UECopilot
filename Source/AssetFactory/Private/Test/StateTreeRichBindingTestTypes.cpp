// Copyright ProjectRPG. All Rights Reserved.

#include "Test/StateTreeRichBindingTestTypes.h"

#include "StateTreeExecutionContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(StateTreeRichBindingTestTypes)

void FAFStateTreeRichBindingInputPropertyFunction::Execute(FStateTreeExecutionContext& Context) const
{
	FAFStateTreeRichBindingInputPropertyFunctionInstanceData& InstanceData =
		Context.GetInstanceData<FAFStateTreeRichBindingInputPropertyFunctionInstanceData>(*this);

	if (const FAFStateTreeRichBindingPayload* Payload = InstanceData.DynamicInput.GetPtr<FAFStateTreeRichBindingPayload>())
	{
		InstanceData.Result = Payload->Value;
		return;
	}

	InstanceData.Result = 0.f;
}
