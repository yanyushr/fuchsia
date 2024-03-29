// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/query_processor.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "peridot/bin/suggestion_engine/suggestion_engine_helper.h"
#include "peridot/lib/fidl/json_xdr.h"

namespace modular {

namespace {

constexpr char kQueryContextKey[] = "/suggestion_engine/current_query";

}  // namespace

QueryProcessor::QueryProcessor(std::shared_ptr<SuggestionDebugImpl> debug)
    : debug_(debug) {}

QueryProcessor::~QueryProcessor() = default;

void QueryProcessor::Initialize(
    fidl::InterfaceHandle<fuchsia::modular::ContextWriter> context_writer) {
  context_writer_.Bind(std::move(context_writer));
}

void QueryProcessor::ExecuteQuery(
    fuchsia::modular::UserInput input, int count,
    fidl::InterfaceHandle<fuchsia::modular::QueryListener> listener) {
  // Process:
  //   1. Close out and clean up any existing query process
  //   2. Update the context engine with the new query
  //   3. Set up the ask variables in suggestion engine
  //   4. Get suggestions from each of the QueryHandlers
  //   5. Filter and Rank the suggestions as received
  //   6. Send "done" to SuggestionListener

  // Step 1
  CleanUpPreviousQuery();

  // Step 2
  std::string query = input.text;
  if (!query.empty() && context_writer_.is_bound()) {
    // Update context engine
    std::string formattedQuery;

    XdrFilterType<std::string> filter_list[] = {
        XdrFilter<std::string>,
        nullptr,
    };

    XdrWrite(&formattedQuery, &query, filter_list);
    context_writer_->WriteEntityTopic(kQueryContextKey, formattedQuery);

    // Update suggestion engine debug interface
    debug_->OnAskStart(query, &suggestions_);
  }

  // Steps 3 - 6
  activity_ = debug_->GetIdleWaiter()->RegisterOngoingActivity();
  active_query_ = std::make_unique<QueryRunner>(std::move(listener),
                                                std::move(input), count);
  active_query_->SetResponseCallback(
      [this, input](const std::string& handler_url,
                    fuchsia::modular::QueryResponse response) {
        OnQueryResponse(input, handler_url, std::move(response));
      });
  active_query_->SetEndRequestCallback(
      [this, input] { OnQueryEndRequest(input); });
  active_query_->Run(query_handlers_);
}

void QueryProcessor::RegisterQueryHandler(
    fidl::StringPtr url, fidl::InterfaceHandle<fuchsia::modular::QueryHandler>
                             query_handler_handle) {
  auto query_handler = query_handler_handle.Bind();
  query_handlers_.emplace_back(std::move(query_handler), url);

  fuchsia::modular::QueryHandlerPtr& handler = query_handlers_.back().handler;
  handler.set_error_handler([this, ptr = handler.get()](zx_status_t status) {
    auto it = std::find_if(query_handlers_.begin(), query_handlers_.end(),
                           [ptr](const QueryHandlerRecord& record) {
                             return record.handler.get() == ptr;
                           });

    FXL_DCHECK(it != query_handlers_.end());
    auto to_erase = query_handlers_.end() - 1;
    std::iter_swap(it, to_erase);
    query_handlers_.erase(to_erase);
  });
}

void QueryProcessor::SetFilters(
    std::vector<std::unique_ptr<SuggestionActiveFilter>>&& active_filters,
    std::vector<std::unique_ptr<SuggestionPassiveFilter>>&& passive_filters) {
  suggestions_.SetActiveFilters(std::move(active_filters));
  suggestions_.SetPassiveFilters(std::move(passive_filters));
}

void QueryProcessor::SetRanker(std::unique_ptr<Ranker> ranker) {
  suggestions_.SetRanker(std::move(ranker));
}

RankedSuggestion* QueryProcessor::GetSuggestion(
    const std::string& suggestion_uuid) const {
  return suggestions_.GetSuggestion(suggestion_uuid);
}

void QueryProcessor::CleanUpPreviousQuery() {
  active_query_.reset();
  suggestions_.RemoveAllSuggestions();
}

void QueryProcessor::AddProposal(const std::string& source_url,
                                 fuchsia::modular::Proposal proposal) {
  suggestions_.RemoveProposal(source_url, proposal.id);

  SuggestionPrototype* suggestion =
      CreateSuggestionPrototype(&query_prototypes_, source_url,
                                "" /* Emtpy story_id */, std::move(proposal));
  suggestions_.AddSuggestion(suggestion);
}

void QueryProcessor::OnQueryResponse(fuchsia::modular::UserInput input,
                                     const std::string& handler_url,
                                     fuchsia::modular::QueryResponse response) {
  // Ranking currently happens as each set of proposals are added.
  for (size_t i = 0; i < response.proposals.size(); ++i) {
    AddProposal(handler_url, std::move(response.proposals.at(i)));
  }
  suggestions_.Refresh(input);

  // Update the fuchsia::modular::QueryListener with new results
  NotifyOfResults();

  // Update the suggestion engine debug interface
  debug_->OnAskStart(input.text, &suggestions_);
}

void QueryProcessor::OnQueryEndRequest(fuchsia::modular::UserInput input) {
  debug_->OnAskStart(input.text, &suggestions_);
  // Idle immediately, we no longer handle audio responses here.
  activity_ = nullptr;
}

void QueryProcessor::NotifyOfResults() {
  const auto& suggestion_vector = suggestions_.Get();

  std::vector<fuchsia::modular::Suggestion> window;
  for (size_t i = 0;
       i < active_query_->max_results() && i < suggestion_vector.size(); i++) {
    window.push_back(CreateSuggestion(*suggestion_vector[i]));
  }

  if (!window.empty()) {
    active_query_->listener()->OnQueryResults(std::move(window));
  }
}

}  // namespace modular
