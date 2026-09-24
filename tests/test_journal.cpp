#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include "core/journal/journal.h"
#include "core/toolcall/toolcall.h"

namespace fs = std::filesystem;
using core::ToolCall;
using core::journal::Entry;

static std::string dump(const ToolCall& tc) {
  return core::journal::sanitizedInput(tc).dump();
}

TEST(Journal, BashCommandKeptVerbatim) {
  auto tc = ToolCall::make(core::tc::Bash, support::Json::object{{"command", "pdftotext a.pdf -"}});
  EXPECT_NE(dump(tc).find("pdftotext a.pdf -"), std::string::npos)
      << "the Bash command must be kept verbatim";
}

TEST(Journal, WriteContentNeverRecorded) {
  auto tc = ToolCall::make(core::tc::Write, support::Json::object{
      {"file_path", "/a.txt"}, {"content", "SECRET_TOKEN=abc123"}});
  auto s = dump(tc);
  EXPECT_EQ(s.find("SECRET_TOKEN"), std::string::npos)
      << "Write content must never enter the journal";
  EXPECT_NE(s.find("/a.txt"), std::string::npos) << "the path should survive";
  EXPECT_NE(s.find("content_sha256"), std::string::npos);
  EXPECT_NE(s.find("bytes"), std::string::npos);
}

TEST(Journal, EditContentNeverRecorded) {
  auto tc = ToolCall::make(core::tc::Edit, support::Json::object{
      {"file_path", "/a.txt"}, {"old_string", "s3cret"}, {"new_string", "n3w"}});
  auto s = dump(tc);
  EXPECT_EQ(s.find("s3cret"), std::string::npos);
  EXPECT_EQ(s.find("n3w"), std::string::npos);
}

TEST(Journal, ReadPathOnly) {
  auto tc = ToolCall::make(core::tc::Read, support::Json::object{
      {"file_path", "/x/.env"}, {"extra", "y"}});
  auto s = dump(tc);
  EXPECT_NE(s.find("/x/.env"), std::string::npos);
  EXPECT_EQ(s.find("extra"), std::string::npos) << "Read should record the path only";
}

TEST(Journal, MCPValuesHashed) {
  auto tc = ToolCall::make("mcp:x:tool", support::Json::object{
      {"token", "very-secret-value"}, {"n", support::Json(3.0)}});
  EXPECT_EQ(dump(tc).find("very-secret-value"), std::string::npos)
      << "MCP argument values must never be stored verbatim";
}

TEST(Journal, Sha256KnownVector) {
  // SHA-256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
  EXPECT_EQ(core::sha256::hex("abc"),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_EQ(core::sha256::hex(""),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(Journal, AppendAndReadAllRoundTrip) {
  auto dir = fs::temp_directory_path() / "warden-journal-rt";
  std::filesystem::remove_all(dir);
  Entry e;
  e.ev = "tool"; e.tool = "bash";
  e.scopes = {"pdf-tools"}; e.caps = {"shell"};
  e.decision = "allow"; e.durMs = 3;
  ASSERT_TRUE(core::journal::append(dir.string(), "s1", e));
  auto got = core::journal::readAll(dir.string(), "s1");
  ASSERT_EQ(got.size(), 1u);
  EXPECT_EQ(got[0].tool, "bash");
  ASSERT_EQ(got[0].scopes.size(), 1u);
  EXPECT_EQ(got[0].scopes[0], "pdf-tools");
}

TEST(Journal, ReadAllMissingFileIsEmptyNotError) {
  auto got = core::journal::readAll(
      (fs::temp_directory_path() / "warden-journal-none").string(), "nope");
  EXPECT_TRUE(got.empty());
}

// Final safety net: splicing sessionId straight into a path lets "../evil"
// escape .warden/journal/.
TEST(Journal, PathEscapeSessionIdRejected) {
  auto root = fs::temp_directory_path() / "warden-journal-escape";
  std::filesystem::remove_all(root);
  Entry e;
  e.ev = "tool";
  e.tool = "bash";

  // root/.warden/journal/ + "../evil.jsonl" -> root/.warden/evil.jsonl (escape).
  EXPECT_FALSE(core::journal::append(root.string(), "../evil", e));
  EXPECT_FALSE(fs::exists(root / ".warden" / "evil.jsonl"))
      << "an illegal session_id must not write files outside the journal directory";
  EXPECT_TRUE(core::journal::readAll(root.string(), "../evil").empty());
  std::filesystem::remove_all(root);
}
