#pragma once

#include "HMIMinimal.h"
#include "OpenAITypes.generated.h"

// ---- Request types ----

USTRUCT(BlueprintType, Category="OpenAI")
struct HMI_API FOpenAIResponseFormat
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FString type; // "text", "json_object", "json_schema"

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FString json_schema; // raw JSON schema string when type == "json_schema"
};

USTRUCT(BlueprintType, Category="OpenAI")
struct HMI_API FOpenAIStreamOptions
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	bool include_usage = false;
};

USTRUCT(BlueprintType, Category="OpenAI")
struct HMI_API FOpenAIToolFunction
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FString name;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FString description;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FString parameters; // raw JSON schema of params
};

USTRUCT(BlueprintType, Category="OpenAI")
struct HMI_API FOpenAITool
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FString type; // "function"

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FOpenAIToolFunction function;
};

USTRUCT(BlueprintType, Category="OpenAI")
struct HMI_API FOpenAIChatMsg
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FString role;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FString content;

	// Optional: tool call id (for tool results)
	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FString tool_call_id;

	// Optional: name
	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FString name;
};

USTRUCT(BlueprintType, Category="OpenAI")
struct HMI_API FOpenAIChatCompletionRequest
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FString model;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	TArray<FOpenAIChatMsg> messages;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	bool stream = false;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FOpenAIResponseFormat response_format;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FOpenAIStreamOptions stream_options;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	TArray<FOpenAITool> tools;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FString tool_choice;
};

// ---- Response types ----

USTRUCT(BlueprintType, Category="OpenAI")
struct HMI_API FOpenAIError
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FString code;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FString message;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FString type;
};

USTRUCT(BlueprintType, Category="OpenAI")
struct HMI_API FOpenAIUsage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	int prompt_tokens = 0;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	int completion_tokens = 0;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	int total_tokens = 0;
};

USTRUCT(BlueprintType, Category="OpenAI")
struct HMI_API FOpenAIToolCallFunction
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FString name;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FString arguments;
};

USTRUCT(BlueprintType, Category="OpenAI")
struct HMI_API FOpenAIToolCall
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	int index = 0;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FString id;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FString type;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FOpenAIToolCallFunction function;
};

USTRUCT(BlueprintType, Category="OpenAI")
struct HMI_API FOpenAIChatChoice
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	int index = 0;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FOpenAIChatMsg delta;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FOpenAIChatMsg message;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FString finish_reason;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	TArray<FOpenAIToolCall> tool_calls;
};

USTRUCT(BlueprintType, Category="OpenAI")
struct HMI_API FOpenAIChatCompletionResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FOpenAIError error;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FString id;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FString object;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	int created = 0;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FString model;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FString system_fingerprint;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	TArray<FOpenAIChatChoice> choices;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FOpenAIUsage usage;
};

USTRUCT(BlueprintType, Category="OpenAI")
struct HMI_API FOpenAIModel
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FString id;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FString object;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	int created = 0;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FString owned_by;
};

USTRUCT(BlueprintType, Category="OpenAI")
struct HMI_API FOpenAIModelListResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FOpenAIError error;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	FString object;

	UPROPERTY(BlueprintReadWrite, Category="OpenAI")
	TArray<FOpenAIModel> data;
};
