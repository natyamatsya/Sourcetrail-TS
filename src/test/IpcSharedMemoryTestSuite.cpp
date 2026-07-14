#include "Catch2.hpp"

#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include <flatbuffers/flatbuffers.h>

#include "IpcSharedMemory.h"
#include "intermediate_storage_generated.h"

TEST_CASE("ipc shared memory")
{
	using enum IpcSharedMemory::AccessMode;
	const std::string memName = "ipc_test";

	// Ensure clean state
	IpcSharedMemory::deleteSharedMemory(memName);

	SECTION("create and open")
	{
		IpcSharedMemory creator(memName, 4096, CREATE_AND_DELETE);

		{
			IpcSharedMemory::ScopedAccess access(&creator);
			REQUIRE(access.size() >= 4096);
			REQUIRE(access.data() != nullptr);
		}
	}

	SECTION("live-handle registry tracks in-process instances per name")
	{
		REQUIRE(IpcSharedMemory::liveHandleCount(memName) == 0);
		{
			IpcSharedMemory first(memName, 4096, CREATE_AND_DELETE);
			REQUIRE(IpcSharedMemory::liveHandleCount(memName) == 1);
			{
				IpcSharedMemory reader(memName, 4096, OPEN_OR_CREATE);
				REQUIRE(IpcSharedMemory::liveHandleCount(memName) == 2);

				// A second CREATE_AND_DELETE owner logs the dangling-views
				// warning (checked by count here; the message goes to the log).
				// Neither `first` nor `reader` is used afterwards — using them
				// past this point is exactly the misuse the guard flags.
				IpcSharedMemory second(memName, 4096, CREATE_AND_DELETE);
				REQUIRE(IpcSharedMemory::liveHandleCount(memName) == 3);
			}
			REQUIRE(IpcSharedMemory::liveHandleCount(memName) == 1);
		}
		REQUIRE(IpcSharedMemory::liveHandleCount(memName) == 0);
	}

	SECTION("write and read raw bytes")
	{
		IpcSharedMemory memory(memName, 4096, CREATE_AND_DELETE);

		const std::string payload = "hello shared memory";

		{
			IpcSharedMemory::ScopedAccess access(&memory);
			access.write(
				reinterpret_cast<const uint8_t*>(payload.data()), payload.size() + 1);
		}

		{
			IpcSharedMemory::ScopedAccess access(&memory);
			std::size_t len = 0;
			const uint8_t* buf = access.read(&len);
			REQUIRE(len >= payload.size() + 1);
			REQUIRE(std::string(reinterpret_cast<const char*>(buf)) == payload);
		}
	}

	SECTION("mutex guards concurrent access")
	{
		IpcSharedMemory creator(memName, 4096, CREATE_AND_DELETE);

		// Initialize counter to 0
		{
			IpcSharedMemory::ScopedAccess access(&creator);
			int32_t zero = 0;
			access.write(reinterpret_cast<const uint8_t*>(&zero), sizeof(zero));
		}

		std::vector<std::shared_ptr<std::thread>> threads;
		for (int i = 0; i < 4; i++)
		{
			threads.push_back(std::make_shared<std::thread>([&memName]() {
				IpcSharedMemory opener(memName, 4096, OPEN_OR_CREATE);
				IpcSharedMemory::ScopedAccess access(&opener);

				int32_t* counter = static_cast<int32_t*>(access.data());
				(*counter)++;
			}));
		}

		for (auto& t : threads)
			t->join();

		IpcSharedMemory::ScopedAccess access(&creator);
		const int32_t* counter = static_cast<const int32_t*>(access.data());
		REQUIRE(*counter == 4);
	}

	SECTION("flatbuffer round-trip through shared memory")
	{
		IpcSharedMemory memory(memName, 65536, CREATE_AND_DELETE);

		// Build a FlatBuffer IntermediateStorage
		flatbuffers::FlatBufferBuilder builder(1024);

		auto node = Sourcetrail::Ipc::CreateStorageNode(
			builder, 42, 1 << 7, builder.CreateString("MyClass"));

		auto error = Sourcetrail::Ipc::CreateStorageError(
			builder, 1, builder.CreateString("undefined reference"), builder.CreateString("main.cpp"),
			true, false);

		auto storage = Sourcetrail::Ipc::CreateIntermediateStorage(
			builder,
			100,
			builder.CreateVector(std::vector<flatbuffers::Offset<Sourcetrail::Ipc::StorageNode>>{node}),
			0, 0, 0, 0, 0, 0, 0,
			builder.CreateVector(std::vector<flatbuffers::Offset<Sourcetrail::Ipc::StorageError>>{error}));

		auto queue = Sourcetrail::Ipc::CreateIntermediateStorageQueue(
			builder,
			builder.CreateVector(
				std::vector<flatbuffers::Offset<Sourcetrail::Ipc::IntermediateStorage>>{storage}));

		builder.Finish(queue);

		// Write to shared memory
		{
			IpcSharedMemory::ScopedAccess access(&memory);
			access.write(builder.GetBufferPointer(), builder.GetSize());
		}

		// Read back and verify
		{
			IpcSharedMemory::ScopedAccess access(&memory);
			std::size_t len = 0;
			const uint8_t* buf = access.read(&len);

			auto readQueue = Sourcetrail::Ipc::GetIntermediateStorageQueue(buf);
			REQUIRE(readQueue->storages()->size() == 1);

			auto readStorage = readQueue->storages()->Get(0);
			REQUIRE(readStorage->next_id() == 100);
			REQUIRE(readStorage->nodes()->size() == 1);

			auto readNode = readStorage->nodes()->Get(0);
			REQUIRE(readNode->id() == 42);
			REQUIRE(readNode->type() == (1 << 7));
			REQUIRE(std::string(readNode->serialized_name()->c_str()) == "MyClass");

			REQUIRE(readStorage->errors()->size() == 1);
			auto readError = readStorage->errors()->Get(0);
			REQUIRE(readError->id() == 1);
			REQUIRE(std::string(readError->message()->c_str()) == "undefined reference");
			REQUIRE(readError->fatal() == true);
			REQUIRE(readError->indexed() == false);
		}
	}

	SECTION("cleanup removes shared memory")
	{
		{
			IpcSharedMemory memory(memName, 4096, CREATE_AND_DELETE);
		}
		// After destructor, creating with OPEN_ONLY should fail
		REQUIRE_THROWS(IpcSharedMemory(memName, 4096, OPEN_ONLY));
	}
}
