// Tests for wlw/core/connection.h: Connection (a plain handle, safe after its source is gone),
// ScopedConnection (disconnects on destruction) and CallbackList (changes while it calls).
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "test_helpers.h"
#include "wlw/core/connection.h"

TEST_SUITE_BEGIN("connection");

using namespace wlw;
using core::CallbackList;
using core::Connection;
using core::ScopedConnection;

TEST_CASE("connection: disconnect, twice, and an empty one") {
  CallbackList<int> list;
  int sum = 0;
  Connection connection = list.Add([&sum](int value) { sum += value; });
  list.Call(2);
  CHECK((sum == 2 && connection.Connected() && list.Size() == 1));

  Connection copy = connection;  // copies name the same callback
  copy.Disconnect();
  CHECK((!connection.Connected() && list.Empty()));
  list.Call(5);
  CHECK(sum == 2);
  connection.Disconnect();  // already gone: nothing happens

  Connection empty;
  CHECK(!empty.Connected());
  empty.Disconnect();
}

TEST_CASE("connection: dropping it keeps the callback") {
  CallbackList<> list;
  int calls = 0;
  list.Add([&calls] { ++calls; });  // the handle is thrown away
  list.Call();
  CHECK(calls == 1);
}

TEST_CASE("connection: safe after the source died") {
  Connection connection;
  {
    CallbackList<> list;
    connection = list.Add([] {});
    CHECK(connection.Connected());
  }
  CHECK(!connection.Connected());
  connection.Disconnect();
}

TEST_CASE("scoped connection: disconnects at the end of its scope, moves, releases") {
  CallbackList<> list;
  int calls = 0;
  {
    ScopedConnection scoped = list.Add([&calls] { ++calls; });
    list.Call();
    ScopedConnection moved = std::move(scoped);  // the moved-from one owns nothing now
    CHECK(moved.Connected());
  }
  list.Call();
  CHECK((calls == 1 && list.Empty()));

  Connection kept;
  {
    ScopedConnection scoped = list.Add([&calls] { ++calls; });
    kept = scoped.Release();  // handed back: not disconnected at scope exit
  }
  list.Call();
  CHECK((calls == 2 && kept.Connected()));

  ScopedConnection reassigned = list.Add([] {});
  reassigned = ScopedConnection(list.Add([] {}));  // the first is disconnected
  CHECK(list.Size() == 2);                         // `kept` + the second
}

TEST_CASE("callback list: changes while calling") {
  CallbackList<> list;
  std::vector<int> order;
  Connection self;
  self = list.Add([&] {
    order.push_back(1);
    self.Disconnect();  // removes itself while running
  });
  Connection second;
  list.Add([&] {
    order.push_back(2);
    second.Disconnect();  // removes one not called yet
  });
  second = list.Add([&] { order.push_back(3); });
  list.Add([&] {
    order.push_back(4);
    list.Add([&] { order.push_back(5); });  // added: called from the next Call on
  });
  list.Call();
  CHECK(order == std::vector<int>{1, 2, 4});
  order.clear();
  list.Call();
  CHECK(order == std::vector<int>{2, 4, 5});
}

TEST_SUITE_END();
