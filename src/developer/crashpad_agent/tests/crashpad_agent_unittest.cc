// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/crashpad_agent/crashpad_agent.h"

#include <stdint.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <fuchsia/crash/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/fdio/spawn.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/strings/concatenate.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/zx/job.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <zircon/errors.h>
#include <zircon/time.h>

#include "src/developer/crashpad_agent/config.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace fuchsia {
namespace crash {
namespace {

// We keep the local Crashpad database size under a certain value. As we want to
// check the produced attachments in the database, we should set the size to be
// at least the total size for a single report so that it does not get cleaned
// up before we are able to inspect its attachments.
// For now, a single report should take up to 1MB.
constexpr uint64_t kMaxTotalReportSizeInKb = 1024u;

// Unit-tests the implementation of the fuchsia.crash.Analyzer FIDL interface.
//
// This does not test the environment service. It directly instantiates the
// class, without connecting through FIDL.
class CrashpadAgentTest : public ::testing::Test {
 public:
  // The underlying agent is initialized with a default config, but can
  // be reset via ResetAgent() if a different config is necessary.
  void SetUp() override {
    ResetAgent(Config{/*local_crashpad_database_path=*/database_path_.path(),
                      /*max_crashpad_database_size_in_kb=*/
                      kMaxTotalReportSizeInKb,
                      /*enable_upload_to_crash_server=*/false,
                      /*crash_server_url=*/nullptr});
  }

 protected:
  // Resets the underlying agent using the given |config|.
  void ResetAgent(Config config) {
    // "attachments" should be kept in sync with the value defined in
    // //crashpad/client/crash_report_database_generic.cc
    attachments_dir_ =
        files::JoinPath(config.local_crashpad_database_path, "attachments");
    agent_ = CrashpadAgent::TryCreate(std::move(config));
    FXL_DCHECK(agent_);
  }

  // Checks that there is:
  //   * only one set of attachments
  //   * the set of attachment filenames match the |expected_attachments|
  //   * no attachment is empty
  // in the local Crashpad database.
  void CheckAttachments(const std::vector<std::string>& expected_attachments) {
    const std::vector<std::string> subdirs = GetAttachmentSubdirs();
    // We expect a single crash report to have been generated.
    ASSERT_EQ(subdirs.size(), 1u);

    std::vector<std::string> attachments;
    const std::string report_attachments_dir =
        files::JoinPath(attachments_dir_, subdirs[0]);
    ASSERT_TRUE(files::ReadDirContents(report_attachments_dir, &attachments));
    RemoveCurrentDirectory(&attachments);
    EXPECT_THAT(attachments,
                testing::UnorderedElementsAreArray(expected_attachments));
    for (const std::string& attachment : attachments) {
      uint64_t size;
      ASSERT_TRUE(files::GetFileSize(
          files::JoinPath(report_attachments_dir, attachment), &size));
      EXPECT_GT(size, 0u) << "attachment file '" << attachment
                          << "' shouldn't be empty";
    }
  }

  // Returns all the attachment subdirectories under the over-arching attachment
  // directory. Each subdirectory corresponds to one local crash report.
  std::vector<std::string> GetAttachmentSubdirs() {
    std::vector<std::string> subdirs;
    FXL_CHECK(files::ReadDirContents(attachments_dir_, &subdirs));
    RemoveCurrentDirectory(&subdirs);
    return subdirs;
  }

  std::unique_ptr<CrashpadAgent> agent_;
  files::ScopedTempDir database_path_;

 private:
  void RemoveCurrentDirectory(std::vector<std::string>* dirs) {
    dirs->erase(std::remove(dirs->begin(), dirs->end(), "."), dirs->end());
  }

  std::string attachments_dir_;
};

TEST_F(CrashpadAgentTest, HandleNativeException_C_Basic) {
  // We create a parent job and a child job. The child job will spawn the
  // crashing program and analyze the crash. The parent job is just here to
  // swallow the exception potentially bubbling up from the child job once the
  // exception has been handled by the test agent (today this is the case as the
  // Crashpad exception handler RESUME_TRY_NEXTs the thread).
  zx::job parent_job;
  zx::port parent_exception_port;
  zx::job job;
  zx::port exception_port;
  zx::process process;
  zx::thread thread;

  // Create the child jobs of the current job now so we can bind to the
  // exception port before spawning the crashing program.
  zx::unowned_job current_job(zx_job_default());
  ASSERT_EQ(zx::job::create(*current_job, 0, &parent_job), ZX_OK);
  ASSERT_EQ(zx::port::create(0u, &parent_exception_port), ZX_OK);
  ASSERT_EQ(zx_task_bind_exception_port(parent_job.get(),
                                        parent_exception_port.get(), 0u, 0u),
            ZX_OK);
  ASSERT_EQ(zx::job::create(parent_job, 0, &job), ZX_OK);
  ASSERT_EQ(zx::port::create(0u, &exception_port), ZX_OK);
  ASSERT_EQ(
      zx_task_bind_exception_port(job.get(), exception_port.get(), 0u, 0u),
      ZX_OK);

  // Create child process using our utility program `crasher` that will crash on
  // startup.
  const char* argv[] = {"crasher", nullptr};
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  ASSERT_EQ(fdio_spawn_etc(job.get(), FDIO_SPAWN_CLONE_ALL,
                           "/pkg/bin/crasher_exe", argv, nullptr, 0, nullptr,
                           process.reset_and_get_address(), err_msg),
            ZX_OK)
      << err_msg;

  // Get the one thread from the child process.
  zx_koid_t thread_ids[1];
  size_t num_ids;
  ASSERT_EQ(process.get_info(ZX_INFO_PROCESS_THREADS, thread_ids,
                             sizeof(zx_koid_t), &num_ids, nullptr),
            ZX_OK);
  ASSERT_EQ(num_ids, 1u);
  ASSERT_EQ(process.get_child(thread_ids[0], ZX_RIGHT_SAME_RIGHTS, &thread),
            ZX_OK);

  // Test crash analysis.
  zx_status_t out_status = ZX_ERR_UNAVAILABLE;
  agent_->HandleNativeException(
      std::move(process), std::move(thread), std::move(exception_port),
      [&out_status](zx_status_t status) { out_status = status; });
  EXPECT_EQ(out_status, ZX_OK);
  CheckAttachments({"build.snapshot", "kernel_log"});

  // The parent job just swallows the exception, i.e. not RESUME_TRY_NEXT it,
  // to not trigger the real agent attached to the root job.
  thread.resume_from_exception(
      parent_exception_port,
      0u /*no options to mark the exception as handled*/);

  // We kill the job so that it doesn't try to reschedule the process, which
  // would crash again, but this time would be handled by the real agent
  // attached to the root job as the exception has already been handled by the
  // parent and child jobs.
  job.kill();
}

TEST_F(CrashpadAgentTest, HandleManagedRuntimeException_Dart_Basic) {
  fuchsia::mem::Buffer stack_trace;
  ASSERT_TRUE(fsl::VmoFromString("#0", &stack_trace));
  zx_status_t out_status = ZX_ERR_UNAVAILABLE;
  agent_->HandleManagedRuntimeException(
      ManagedRuntimeLanguage::DART, "component_url", "UnhandledException: Foo",
      std::move(stack_trace),
      [&out_status](zx_status_t status) { out_status = status; });
  EXPECT_EQ(out_status, ZX_OK);
  CheckAttachments({"build.snapshot", "DartError"});
}

TEST_F(CrashpadAgentTest,
       HandleManagedRuntimeException_Dart_ExceptionStringInBadFormat) {
  fuchsia::mem::Buffer stack_trace;
  ASSERT_TRUE(fsl::VmoFromString("#0", &stack_trace));
  zx_status_t out_status = ZX_ERR_UNAVAILABLE;
  agent_->HandleManagedRuntimeException(
      ManagedRuntimeLanguage::DART, "component_url", "wrong format",
      std::move(stack_trace),
      [&out_status](zx_status_t status) { out_status = status; });
  EXPECT_EQ(out_status, ZX_OK);
  CheckAttachments({"build.snapshot", "DartError"});
}

TEST_F(CrashpadAgentTest, HandleManagedRuntimeException_OtherLanguage_Basic) {
  fuchsia::mem::Buffer stack_trace;
  ASSERT_TRUE(fsl::VmoFromString("#0", &stack_trace));
  zx_status_t out_status = ZX_ERR_UNAVAILABLE;
  agent_->HandleManagedRuntimeException(
      ManagedRuntimeLanguage::OTHER_LANGUAGE, "component_url", "error",
      std::move(stack_trace),
      [&out_status](zx_status_t status) { out_status = status; });
  EXPECT_EQ(out_status, ZX_OK);
  CheckAttachments({"build.snapshot", "stack_trace"});
}

TEST_F(CrashpadAgentTest, ProcessKernelPanicCrashlog_Basic) {
  fuchsia::mem::Buffer crashlog;
  ASSERT_TRUE(fsl::VmoFromString("ZIRCON KERNEL PANIC", &crashlog));
  zx_status_t out_status = ZX_ERR_UNAVAILABLE;
  agent_->ProcessKernelPanicCrashlog(
      std::move(crashlog),
      [&out_status](zx_status_t status) { out_status = status; });
  EXPECT_EQ(out_status, ZX_OK);
  CheckAttachments({"build.snapshot", "log"});
}

TEST_F(CrashpadAgentTest, PruneDatabase_ZeroSize) {
  // We reset the agent with a max database size of 0, meaning reports will
  // get cleaned up before the end of the |agent_| call.
  ResetAgent(Config{/*local_crashpad_database_path=*/database_path_.path(),
                    /*max_crashpad_database_size_in_kb=*/0u,
                    /*enable_upload_to_crash_server=*/false,
                    /*crash_server_url=*/nullptr});

  // We generate a crash report, using ProcessKernelPanicCrashlog() because
  // there are fewer arguments!
  fuchsia::mem::Buffer crashlog;
  ASSERT_TRUE(
      fsl::VmoFromString("just big enough to be greater than 0", &crashlog));
  zx_status_t out_status = ZX_ERR_UNAVAILABLE;
  agent_->ProcessKernelPanicCrashlog(
      std::move(crashlog),
      [&out_status](zx_status_t status) { out_status = status; });
  EXPECT_EQ(out_status, ZX_OK);

  // We check that all the attachments have been cleaned up.
  EXPECT_TRUE(GetAttachmentSubdirs().empty());
}

std::string GenerateString(const uint64_t string_size_in_kb) {
  std::string str;
  for (size_t i = 0; i < string_size_in_kb * 1024; ++i) {
    str.push_back(static_cast<char>(i % 128));
  }
  return str;
}

TEST_F(CrashpadAgentTest, PruneDatabase_SizeForOneReport) {
  // We reset the agent with a max database size equivalent to the expected
  // size of a report plus the value of an especially large attachment.
  const uint64_t crashlog_size_in_kb = 2u * kMaxTotalReportSizeInKb;
  const std::string large_string = GenerateString(crashlog_size_in_kb);
  ResetAgent(
      Config{/*local_crashpad_database_path=*/database_path_.path(),
             /*max_crashpad_database_size_in_kb=*/kMaxTotalReportSizeInKb +
                 crashlog_size_in_kb,
             /*enable_upload_to_crash_server=*/false,
             /*crash_server_url=*/nullptr});

  // We generate a crash report, using ProcessKernelPanicCrashlog() because
  // we can more easily control the total report size as there are no minidumps
  // and the crashlog is one of the attachments.
  fuchsia::mem::Buffer crashlog;
  ASSERT_TRUE(fsl::VmoFromString(large_string, &crashlog));
  zx_status_t out_status = ZX_ERR_UNAVAILABLE;
  agent_->ProcessKernelPanicCrashlog(
      std::move(crashlog),
      [&out_status](zx_status_t status) { out_status = status; });
  EXPECT_EQ(out_status, ZX_OK);

  // We check that only one set of attachments is there.
  const std::vector<std::string> attachment_subdirs = GetAttachmentSubdirs();
  ASSERT_EQ(attachment_subdirs.size(), 1u);

  // We sleep for one second to guarantee a different creation time for the
  // next crash report.
  zx_nanosleep(zx_deadline_after(ZX_SEC(1)));

  // We generate a new crash report.
  fuchsia::mem::Buffer new_crashlog;
  ASSERT_TRUE(fsl::VmoFromString(large_string, &new_crashlog));
  zx_status_t new_out_status = ZX_ERR_UNAVAILABLE;
  agent_->ProcessKernelPanicCrashlog(
      std::move(new_crashlog),
      [&new_out_status](zx_status_t status) { new_out_status = status; });
  EXPECT_EQ(new_out_status, ZX_OK);

  // We check that only one set of attachments is there and that it is a
  // different directory than previously (the directory name is the local crash
  // report ID).
  const std::vector<std::string> new_attachment_subdirs =
      GetAttachmentSubdirs();
  EXPECT_EQ(new_attachment_subdirs.size(), 1u);
  EXPECT_THAT(
      new_attachment_subdirs,
      testing::Not(testing::UnorderedElementsAreArray(attachment_subdirs)));
}

}  // namespace
}  // namespace crash
}  // namespace fuchsia

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  syslog::InitLogger({"crash", "test"});
  return RUN_ALL_TESTS();
}