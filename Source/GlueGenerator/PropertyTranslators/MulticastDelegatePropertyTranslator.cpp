﻿#include "MulticastDelegatePropertyTranslator.h"

#include "GlueGenerator/CSGenerator.h"
#include "GlueGenerator/CSScriptBuilder.h"

bool FMulticastDelegatePropertyTranslator::CanHandleProperty(const FProperty* Property) const
{
	const FMulticastDelegateProperty* DelegateProperty = CastField<FMulticastDelegateProperty>(Property);

	if (!FCSGenerator::Get().CanExportFunctionParameters(DelegateProperty->SignatureFunction))
	{
		return false;	
	}
	
	return true;
}

FString FMulticastDelegatePropertyTranslator::GetManagedType(const FProperty* Property) const
{
	return Property->GetName();
}

void FMulticastDelegatePropertyTranslator::ExportPropertyStaticConstruction(FCSScriptBuilder& Builder,
	const FProperty* Property,
	const FString& NativePropertyName) const
{
	Builder.AppendLine(FString::Printf(TEXT("%s_NativeProperty = %s.CallGetNativePropertyFromName(NativeClassPtr, \"%s\");"), *NativePropertyName, FPropertyCallbacks, *NativePropertyName));
	FPropertyTranslator::ExportPropertyStaticConstruction(Builder, Property, NativePropertyName);

	const FMulticastDelegateProperty* DelegateProperty = CastFieldChecked<FMulticastDelegateProperty>(Property);
	
	if (DelegateProperty->SignatureFunction->NumParms > 0)
	{
		Builder.AppendLine(FString::Printf(TEXT("%s.InitializeUnrealDelegate(%s_NativeProperty);"), *NativePropertyName, *NativePropertyName));
	}
}

void FMulticastDelegatePropertyTranslator::ExportPropertyVariables(FCSScriptBuilder& Builder, const FProperty* Property, const FString& PropertyName) const
{
	Builder.AppendLine(FString::Printf(TEXT("static IntPtr %s_NativeProperty;"), *PropertyName));
	FPropertyTranslator::ExportPropertyVariables(Builder, Property, PropertyName);
}

void FMulticastDelegatePropertyTranslator::ExportPropertySetter(FCSScriptBuilder& Builder, const FProperty* Property, const FString& PropertyName) const
{
	Builder.AppendLine(FString::Printf(TEXT("DelegateMarshaller<%s>.ToNative(IntPtr.Add(NativeObject,%s_Offset),0,this,value);"), *PropertyName, *PropertyName));
}

void FMulticastDelegatePropertyTranslator::ExportPropertyGetter(FCSScriptBuilder& Builder, const FProperty* Property, const FString& PropertyName) const
{
	Builder.AppendLine(FString::Printf(TEXT("return DelegateMarshaller<%s>.FromNative(IntPtr.Add(NativeObject,%s_Offset),0,this);"), *PropertyName, *PropertyName));
}

FString FMulticastDelegatePropertyTranslator::GetNullReturnCSharpValue(const FProperty* ReturnProperty) const
{
	return "null";
}

void FMulticastDelegatePropertyTranslator::OnPropertyExported(FCSScriptBuilder& Builder, const FProperty* Property, const FString& PropertyName) const
{
	FCSModule& Module = FCSGenerator::Get().FindOrRegisterModule(Property->GetOutermost());
	const FMulticastDelegateProperty* DelegateProperty = CastFieldChecked<FMulticastDelegateProperty>(Property);
	UFunction* Function = DelegateProperty->SignatureFunction;
	
	FCSScriptBuilder DelegateBuilder(FCSScriptBuilder::IndentType::Spaces);

	DelegateBuilder.GenerateScriptSkeleton(Module.GetNamespace());
	DelegateBuilder.AppendLine();

	FString SignatureName = FString::Printf(TEXT("%s.Signature"), *PropertyName);
	FString SuperClass = FString::Printf(TEXT("MulticastDelegate<%s>"), *SignatureName);
	
	DelegateBuilder.DeclareType("class", PropertyName, SuperClass, false, true);
	
	if (Function->NumParms > 0)
	{
		FunctionExporter Exporter(*this, *Function, ProtectionMode::UseUFunctionProtection, OverloadMode::SuppressOverloads, BlueprintVisibility::Call);
		DelegateBuilder.AppendLine(FString::Printf(TEXT("public delegate void Signature(%s);"), *Exporter.ParamsStringAPIWithDefaults));
		DelegateBuilder.AppendLine();
		
		Exporter.ExportFunctionVariables(DelegateBuilder);
		DelegateBuilder.AppendLine();
		
		DelegateBuilder.AppendLine(FString::Printf(TEXT("protected void Invoker(%s)"), *Exporter.ParamsStringAPIWithDefaults));
		DelegateBuilder.OpenBrace();
		DelegateBuilder.BeginUnsafeBlock();
		Exporter.ExportInvoke(DelegateBuilder, FunctionExporter::InvokeMode::Normal);
		DelegateBuilder.EndUnsafeBlock();
		DelegateBuilder.CloseBrace();

		DelegateBuilder.AppendLine("static public void InitializeUnrealDelegate(IntPtr nativeDelegateProperty)");
		DelegateBuilder.OpenBrace();
		FCSGenerator::Get().ExportDelegateFunctionStaticConstruction(DelegateBuilder, Function);
		DelegateBuilder.CloseBrace();
	}
	else
	{
		DelegateBuilder.AppendLine("public delegate void Signature();");
	}
	
	DelegateBuilder.CloseBrace();
	
	FString FileName = FString::Printf(TEXT("%s.generated.cs"), *PropertyName);
	FCSGenerator::Get().SaveGlue(&Module, FileName, DelegateBuilder.ToString());
}
