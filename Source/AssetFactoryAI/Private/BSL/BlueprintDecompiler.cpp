// Copyright Epic Games, Inc. All Rights Reserved.

#include "BSL/BlueprintDecompiler.h"

// UE Blueprint 核心
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"

// K2 节点类型
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"

// 用于识别 exec pin 的类型分类字符串
#include "EdGraphSchema_K2.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_Self.h"

// 文件本地辅助：将 Pin 的 DefaultValue / DefaultTextValue 直接转换为字面量表达式
// 不追溯连接，不查所属节点，因此不会引发递归
static TSharedPtr<BSL::FExpression> MakeLiteralFromPin(UEdGraphPin* Pin)
{
	if (!Pin) return nullptr;
	const FString& DV = Pin->DefaultValue;
	const FString PinCat = Pin->PinType.PinCategory.ToString();

	if (!DV.IsEmpty())
	{
		if (PinCat == TEXT("bool"))
			return BSL::FExpression::MakeBool(DV.ToBool());
		if (PinCat == TEXT("int") || PinCat == TEXT("int64") || PinCat == TEXT("byte"))
			return BSL::FExpression::MakeInt(FCString::Atoi(*DV));
		if (PinCat == TEXT("float") || PinCat == TEXT("double") || PinCat == TEXT("real"))
			return BSL::FExpression::MakeFloat(FCString::Atof(*DV));
		if (PinCat == TEXT("string") || PinCat == TEXT("name") || PinCat == TEXT("text"))
			return BSL::FExpression::MakeString(DV);
	}
	if (!Pin->DefaultTextValue.IsEmpty())
		return BSL::FExpression::MakeString(Pin->DefaultTextValue.ToString());

	return nullptr;
}

namespace BSL
{

// ============================================================
// 公共静态入口
// ============================================================

FDecompileResult FBlueprintDecompiler::Decompile(UBlueprint* BP)
{
	FDecompileResult Result;

	if (!BP)
	{
		Result.Errors.Add(TEXT("Null Blueprint passed to FBlueprintDecompiler::Decompile"));
		return Result;
	}

	Result.Blueprint.Name = BP->GetName();
	if (BP->ParentClass)
	{
		Result.Blueprint.ParentClass = BP->ParentClass->GetName();
	}

	// ----------------------------------------------------------
	// 1. 成员变量：从 NewVariables 列表还原
	// ----------------------------------------------------------
	for (const FBPVariableDescription& VarDesc : BP->NewVariables)
	{
		FVariable Var;
		Var.Name = VarDesc.VarName.ToString();

		// 映射 UE pin 类型分类 -> BSL EType
		const FName Cat = VarDesc.VarType.PinCategory;
		if      (Cat == UEdGraphSchema_K2::PC_Boolean) Var.Type.Type = EType::Bool;
		else if (Cat == UEdGraphSchema_K2::PC_Int)     Var.Type.Type = EType::Int;
		else if (Cat == UEdGraphSchema_K2::PC_Float)   Var.Type.Type = EType::Float;
		else if (Cat == UEdGraphSchema_K2::PC_Double)  Var.Type.Type = EType::Float;
		else if (Cat == UEdGraphSchema_K2::PC_String)  Var.Type.Type = EType::String;
		else if (Cat == UEdGraphSchema_K2::PC_Name)    Var.Type.Type = EType::Name;
		else if (Cat == UEdGraphSchema_K2::PC_Text)    Var.Type.Type = EType::Text;
		else if (Cat == UEdGraphSchema_K2::PC_Object)
		{
			Var.Type.Type = EType::Object;
			if (VarDesc.VarType.PinSubCategoryObject.IsValid())
				Var.Type.SubType = VarDesc.VarType.PinSubCategoryObject->GetName();
		}
		else if (Cat == UEdGraphSchema_K2::PC_Class)
		{
			Var.Type.Type = EType::Class;
			if (VarDesc.VarType.PinSubCategoryObject.IsValid())
				Var.Type.SubType = VarDesc.VarType.PinSubCategoryObject->GetName();
		}
		else
		{
			Var.Type.Type    = EType::Unknown;
			Var.Type.SubType = Cat.ToString();
		}

		// 默认值：若有则生成字符串字面量
		if (!VarDesc.DefaultValue.IsEmpty())
		{
			Var.DefaultValue = FExpression::MakeString(VarDesc.DefaultValue);
		}

		Result.Blueprint.Variables.Add(Var);
	}

	// ----------------------------------------------------------
	// 2. EventGraph（UbergraphPages）：找所有 Event 节点
	// ----------------------------------------------------------
	for (UEdGraph* Graph : BP->UbergraphPages)
	{
		if (!Graph) continue;

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
			if (!EventNode) continue;

			FFunction Func;
			Func.bIsEvent = true;
			Func.Name     = EventNode->EventReference.GetMemberName().ToString();

			// 如果 Name 为空（自定义事件），回退到节点名称
			if (Func.Name.IsEmpty())
			{
				Func.Name = EventNode->GetNodeTitle(ENodeTitleType::MenuTitle).ToString();
			}

			// 收集事件输出参数 pin（非 exec，非 self，direction=Output）
			for (UEdGraphPin* Pin : EventNode->Pins)
			{
				if (Pin->Direction != EGPD_Output) continue;
				if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;
				if (Pin->PinName == UEdGraphSchema_K2::PN_Self) continue;

				FVariable OutParam;
				OutParam.Name = Pin->GetName();
				// 类型映射（与上面成员变量相同逻辑）
				const FName PinCat = Pin->PinType.PinCategory;
				if      (PinCat == UEdGraphSchema_K2::PC_Boolean) OutParam.Type.Type = EType::Bool;
				else if (PinCat == UEdGraphSchema_K2::PC_Int)     OutParam.Type.Type = EType::Int;
				else if (PinCat == UEdGraphSchema_K2::PC_Float)   OutParam.Type.Type = EType::Float;
				else if (PinCat == UEdGraphSchema_K2::PC_Double)  OutParam.Type.Type = EType::Float;
				else if (PinCat == UEdGraphSchema_K2::PC_String)  OutParam.Type.Type = EType::String;
				else if (PinCat == UEdGraphSchema_K2::PC_Name)    OutParam.Type.Type = EType::Name;
				else if (PinCat == UEdGraphSchema_K2::PC_Text)    OutParam.Type.Type = EType::Text;
				else                                              OutParam.Type.Type = EType::Unknown;
				Func.Inputs.Add(OutParam);
			}

			// 沿 "then" pin 执行链生成函数体
			FBlueprintDecompiler Decompiler;
			UEdGraphPin* ThenPin = EventNode->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
			if (ThenPin)
			{
				Func.Body = Decompiler.WalkExecChain(ThenPin);
			}

			Result.Warnings.Append(Decompiler.Warnings);
			Result.Blueprint.Functions.Add(Func);
		}
	}

	// ----------------------------------------------------------
	// 3. FunctionGraphs：每个图一个 FFunction
	// ----------------------------------------------------------
	for (UEdGraph* Graph : BP->FunctionGraphs)
	{
		if (!Graph) continue;

		FFunction Func;
		Func.bIsEvent = false;
		Func.Name     = Graph->GetName();

		FBlueprintDecompiler Decompiler;
		Decompiler.DecompileGraph(Graph, Func);
		Result.Warnings.Append(Decompiler.Warnings);
		Result.Blueprint.Functions.Add(Func);
	}

	Result.bSuccess = true;
	return Result;
}

// ============================================================
// 私有：反编译函数图
// ============================================================

void FBlueprintDecompiler::DecompileGraph(UEdGraph* Graph, FFunction& OutFunc)
{
	if (!Graph) return;

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(Node);
		if (!EntryNode) continue;

		// 收集函数输入参数（FunctionEntry 的输出 pin 即调用者传入的参数）
		for (UEdGraphPin* Pin : EntryNode->Pins)
		{
			if (Pin->Direction != EGPD_Output) continue;
			if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;
			if (Pin->PinName == UEdGraphSchema_K2::PN_Self) continue;

			FVariable InParam;
			InParam.Name = Pin->GetName();
			const FName PinCat = Pin->PinType.PinCategory;
			if      (PinCat == UEdGraphSchema_K2::PC_Boolean) InParam.Type.Type = EType::Bool;
			else if (PinCat == UEdGraphSchema_K2::PC_Int)     InParam.Type.Type = EType::Int;
			else if (PinCat == UEdGraphSchema_K2::PC_Float)   InParam.Type.Type = EType::Float;
			else if (PinCat == UEdGraphSchema_K2::PC_Double)  InParam.Type.Type = EType::Float;
			else if (PinCat == UEdGraphSchema_K2::PC_String)  InParam.Type.Type = EType::String;
			else if (PinCat == UEdGraphSchema_K2::PC_Name)    InParam.Type.Type = EType::Name;
			else if (PinCat == UEdGraphSchema_K2::PC_Text)    InParam.Type.Type = EType::Text;
			else                                              InParam.Type.Type = EType::Unknown;

			OutFunc.Inputs.Add(InParam);
		}

		// 从入口的 "then" pin 开始遍历执行链
		UEdGraphPin* ThenPin = EntryNode->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
		if (ThenPin)
		{
			OutFunc.Body = WalkExecChain(ThenPin);
		}
		return; // 一个图只有一个 FunctionEntry
	}
}

// ============================================================
// 私有：沿执行链遍历，生成语句列表
// ============================================================

TArray<TSharedPtr<FStatement>> FBlueprintDecompiler::WalkExecChain(UEdGraphPin* ExecPin)
{
	TArray<TSharedPtr<FStatement>> Stmts;
	if (!ExecPin || ExecPin->LinkedTo.Num() == 0) return Stmts;

	// 使用 GetOwningNodeUnchecked() 防御损坏的 pin（GetOwningNode 内部有 check() 断言）
	UEdGraphNode* NextNode = ExecPin->LinkedTo[0]->GetOwningNodeUnchecked();

	// 防止循环图造成死循环
	TSet<UEdGraphNode*> Visited;

	while (NextNode && !Visited.Contains(NextNode))
	{
		Visited.Add(NextNode);

		TSharedPtr<FStatement> Stmt = NodeToStatement(NextNode);
		if (Stmt.IsValid())
		{
			Stmts.Add(Stmt);
		}

		// If 语句本身已经在 NodeToStatement 内部递归处理了两条分支，
		// 执行链在 If 节点后通常没有 "then" pin（Branch 节点本身没有主线继续）
		// 所以这里只处理非 Branch 节点的线性继续
		if (Cast<UK2Node_IfThenElse>(NextNode))
		{
			// Branch 节点没有主线继续，停止遍历
			break;
		}

		// 查找当前节点的 "then" exec 输出 pin，继续遍历
		UEdGraphPin* ThenPin = NextNode->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
		if (!ThenPin || ThenPin->LinkedTo.Num() == 0)
		{
			break;
		}
		// 使用 GetOwningNodeUnchecked() 保持与 PinToExpression 的防御性一致
		NextNode = ThenPin->LinkedTo[0]->GetOwningNodeUnchecked();
	}

	return Stmts;
}

// ============================================================
// 私有：单节点 -> FStatement
// ============================================================

TSharedPtr<FStatement> FBlueprintDecompiler::NodeToStatement(UEdGraphNode* Node)
{
	if (!Node) return nullptr;

	// ----------------------------------------------------------
	// 函数调用节点（CallFunction）
	// ----------------------------------------------------------
	if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
	{
		TSharedPtr<FStatement> Stmt = MakeShared<FStatement>(EStatementType::ExpressionStmt);

		TSharedPtr<FExpression> CallExpr = MakeShared<FExpression>(EExpressionType::FunctionCall);
		CallExpr->Name = CallNode->FunctionReference.GetMemberName().ToString();

		// 收集数据输入 pin（跳过 exec pin 和 self pin）
		for (UEdGraphPin* Pin : CallNode->Pins)
		{
			if (Pin->Direction != EGPD_Input) continue;
			if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;
			if (Pin->PinName == UEdGraphSchema_K2::PN_Self) continue;

			TSharedPtr<FExpression> ArgExpr;
			if (Pin->LinkedTo.Num() > 0)
			{
				// 追溯到连接的源 pin
				ArgExpr = PinToExpression(Pin->LinkedTo[0]);
			}
			else if (!Pin->DefaultValue.IsEmpty())
			{
				// 未连接但有默认值：直接生成字面量
				ArgExpr = PinToExpression(Pin);
			}

			if (ArgExpr.IsValid())
			{
				CallExpr->Arguments.Add(ArgExpr);
			}
		}

		Stmt->Expression = CallExpr;
		return Stmt;
	}

	// ----------------------------------------------------------
	// 条件分支节点（Branch / IfThenElse）
	// ----------------------------------------------------------
	if (UK2Node_IfThenElse* BranchNode = Cast<UK2Node_IfThenElse>(Node))
	{
		TSharedPtr<FStatement> Stmt = MakeShared<FStatement>(EStatementType::If);

		// 条件 pin 固定名为 "Condition"
		UEdGraphPin* CondPin = BranchNode->FindPin(TEXT("Condition"), EGPD_Input);
		if (CondPin && CondPin->LinkedTo.Num() > 0)
		{
			Stmt->Condition = PinToExpression(CondPin->LinkedTo[0]);
		}
		else if (CondPin)
		{
			// 未连接时使用默认值（通常是 false）
			Stmt->Condition = FExpression::MakeBool(CondPin->DefaultValue.ToBool());
		}

		// "True" 分支
		UEdGraphPin* TruePin = BranchNode->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
		if (TruePin)
		{
			Stmt->ThenBody = WalkExecChain(TruePin);
		}

		// "False" 分支（使用 FindPin 而非 GetElsePin，避免 GetElsePin 内部的 check() 在损坏蓝图时 crash）
		UEdGraphPin* FalsePin = BranchNode->FindPin(UEdGraphSchema_K2::PN_Else, EGPD_Output);
		if (FalsePin)
		{
			Stmt->ElseBody = WalkExecChain(FalsePin);
		}

		return Stmt;
	}

	// ----------------------------------------------------------
	// 变量赋值节点（VariableSet）
	// ----------------------------------------------------------
	if (UK2Node_VariableSet* SetNode = Cast<UK2Node_VariableSet>(Node))
	{
		TSharedPtr<FStatement> Stmt = MakeShared<FStatement>(EStatementType::Assignment);
		Stmt->AssignTarget = SetNode->GetVarName().ToString();

		// 值 pin 与变量同名
		UEdGraphPin* ValuePin = SetNode->FindPin(SetNode->GetVarName(), EGPD_Input);
		if (ValuePin && ValuePin->LinkedTo.Num() > 0)
		{
			Stmt->AssignValue = PinToExpression(ValuePin->LinkedTo[0]);
		}
		else if (ValuePin && !ValuePin->DefaultValue.IsEmpty())
		{
			Stmt->AssignValue = PinToExpression(ValuePin);
		}

		return Stmt;
	}

	// ----------------------------------------------------------
	// FunctionReturn 节点：Return 语句
	// ----------------------------------------------------------
	// UK2Node_FunctionResult 是函数的返回节点
	if (UK2Node_FunctionResult* ResultNode = Cast<UK2Node_FunctionResult>(Node))
	{
		TSharedPtr<FStatement> Stmt = MakeShared<FStatement>(EStatementType::Return);

		// 收集所有非 exec 输入 pin 作为返回值
		for (UEdGraphPin* Pin : ResultNode->Pins)
		{
			if (Pin->Direction != EGPD_Input) continue;
			if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;

			TSharedPtr<FExpression> RetExpr;
			if (Pin->LinkedTo.Num() > 0)
			{
				RetExpr = PinToExpression(Pin->LinkedTo[0]);
			}
			else if (!Pin->DefaultValue.IsEmpty())
			{
				RetExpr = PinToExpression(Pin);
			}
			if (RetExpr.IsValid())
			{
				Stmt->ReturnValues.Add(RetExpr);
			}
		}

		return Stmt;
	}

	// ----------------------------------------------------------
	// 兜底：无法识别的节点 -> RawNode 逃生舱
	// ----------------------------------------------------------
	TSharedPtr<FStatement> RawStmt = MakeShared<FStatement>(EStatementType::RawNode);
	RawStmt->RawNodeType = Node->GetClass()->GetName();
	// 将节点注释（如有）作为辅助信息写入 RawNodeParamsJson
	if (!Node->NodeComment.IsEmpty())
	{
		// 对注释中的特殊字符进行 JSON 转义（顺序：先 \ 后 "，再处理控制字符）
		FString SafeComment = Node->NodeComment
			.Replace(TEXT("\\"), TEXT("\\\\"))
			.Replace(TEXT("\""), TEXT("\\\""))
			.Replace(TEXT("\r\n"), TEXT("\\n"))  // Windows CRLF（先处理，避免被后续替换截断）
			.Replace(TEXT("\n"), TEXT("\\n"))
			.Replace(TEXT("\r"), TEXT("\\r"))
			.Replace(TEXT("\t"), TEXT("\\t"));
		RawStmt->RawNodeParamsJson = FString::Printf(TEXT("{\"comment\":\"%s\"}"), *SafeComment);
	}

	Warnings.Add(FString::Printf(TEXT("Unrecognized node type '%s' (title: %s), emitted as @node"),
		*Node->GetClass()->GetName(),
		*Node->GetNodeTitle(ENodeTitleType::MenuTitle).ToString()));

	return RawStmt;
}

// ============================================================
// 私有：Pin -> FExpression
// ============================================================

TSharedPtr<FExpression> FBlueprintDecompiler::PinToExpression(UEdGraphPin* DataPin)
{
	if (!DataPin) return nullptr;

	// 使用 GetOwningNodeUnchecked() 以支持对损坏 pin 的防御性检查（GetOwningNode 内部有 check() 断言）
	UEdGraphNode* SourceNode = DataPin->GetOwningNodeUnchecked();
	if (!SourceNode) return nullptr;
	const FString PinCatStr  = DataPin->PinType.PinCategory.ToString();

	// ----------------------------------------------------------
	// 1. 变量 Get 节点
	// ----------------------------------------------------------
	if (UK2Node_VariableGet* GetNode = Cast<UK2Node_VariableGet>(SourceNode))
	{
		return FExpression::MakeVariable(GetNode->GetVarName().ToString());
	}

	// ----------------------------------------------------------
	// 2. 另一个函数调用的返回值（作为参数传入）
	// ----------------------------------------------------------
	if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(SourceNode))
	{
		TSharedPtr<FExpression> CallExpr = MakeShared<FExpression>(EExpressionType::FunctionCall);
		CallExpr->Name = CallNode->FunctionReference.GetMemberName().ToString();

		// 递归收集该调用节点的输入参数
		for (UEdGraphPin* Pin : CallNode->Pins)
		{
			if (Pin->Direction != EGPD_Input) continue;
			if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;
			if (Pin->PinName == UEdGraphSchema_K2::PN_Self) continue;

			TSharedPtr<FExpression> ArgExpr;
			if (Pin->LinkedTo.Num() > 0)
			{
				ArgExpr = PinToExpression(Pin->LinkedTo[0]);
			}
			else
			{
				ArgExpr = MakeLiteralFromPin(Pin);
			}
			if (ArgExpr.IsValid())
			{
				CallExpr->Arguments.Add(ArgExpr);
			}
		}

		return CallExpr;
	}

	// ----------------------------------------------------------
	// 3. Self 节点
	// ----------------------------------------------------------
	if (UK2Node_Self* SelfNode = Cast<UK2Node_Self>(SourceNode))
	{
		return MakeShared<FExpression>(EExpressionType::Self);
	}

	// ----------------------------------------------------------
	// 4. 字面量：通过 pin 的 DefaultValue / DefaultTextValue 直接转换
	// ----------------------------------------------------------
	if (TSharedPtr<FExpression> Lit = MakeLiteralFromPin(DataPin))
	{
		return Lit;
	}

	// ----------------------------------------------------------
	// 5. 兜底：返回一个注释变量，记录警告
	// ----------------------------------------------------------
	Warnings.Add(FString::Printf(
		TEXT("Cannot convert pin '%s' (category='%s') on node '%s' to expression; using placeholder"),
		*DataPin->GetName(),
		*PinCatStr,
		*SourceNode->GetClass()->GetName()));

	return FExpression::MakeVariable(FString::Printf(TEXT("/* unknown_pin_%s */"), *DataPin->GetName()));
}

} // namespace BSL
