#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"
#include "Misc/AutomationTest.h"
#include "Online/WSDtos.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
TSharedPtr<FJsonObject> Parse(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	FJsonSerializer::Deserialize(Reader, Json);
	return Json;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSDtoParsingTest,
	"WorldSports.Online.DtosParseBackendJson",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSDtoParsingTest::RunTest(const FString&)
{
	// Samples copied from live backend responses — if a schema field is
	// renamed server-side, this test is the client's tripwire.
	FWSTokenResponse Token;
	TestTrue(TEXT("token parses"), FJsonObjectConverter::JsonObjectToUStruct(
		Parse(TEXT("{\"access_token\":\"abc.def.ghi\",\"token_type\":\"bearer\"}")).ToSharedRef(),
		&Token));
	TestEqual(TEXT("token value"), Token.access_token, TEXT("abc.def.ghi"));

	FWSUserDto User;
	TestTrue(TEXT("user parses"), FJsonObjectConverter::JsonObjectToUStruct(
		Parse(TEXT("{\"id\":7,\"email\":\"a@b.c\",\"display_name\":\"Ada\",")
			TEXT("\"created_at\":\"2026-08-15T10:00:00Z\"}")).ToSharedRef(),
		&User));
	TestEqual(TEXT("user id"), User.id, 7);
	TestEqual(TEXT("user name"), User.display_name, TEXT("Ada"));

	FWSResultResponse Result;
	TestTrue(TEXT("result parses"), FJsonObjectConverter::JsonObjectToUStruct(
		Parse(TEXT("{\"accepted\":true,\"rejection_reason\":null,\"event\":\"sprint-100m\",")
			TEXT("\"value_text\":\"10.42\",\"is_personal_best\":true,\"xp_awarded\":25,")
			TEXT("\"total_xp\":125,\"career_stage\":\"amateur\"}")).ToSharedRef(),
		&Result));
	TestTrue(TEXT("accepted"), Result.accepted);
	TestEqual(TEXT("xp comes from the server"), Result.xp_awarded, 25);
	TestEqual(TEXT("null reason is empty"), Result.rejection_reason, FString());

	// A rejected result parses too — rejection is data, not an error path.
	FWSResultResponse Rejected;
	TestTrue(TEXT("rejection parses"), FJsonObjectConverter::JsonObjectToUStruct(
		Parse(TEXT("{\"accepted\":false,\"rejection_reason\":\"False start: reaction 42ms\",")
			TEXT("\"event\":\"sprint-100m\",\"value_text\":\"9.10\",\"is_personal_best\":false,")
			TEXT("\"xp_awarded\":0,\"total_xp\":100,\"career_stage\":\"amateur\"}")).ToSharedRef(),
		&Rejected));
	TestFalse(TEXT("not accepted"), Rejected.accepted);
	TestEqual(TEXT("zero xp on rejection"), Rejected.xp_awarded, 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
