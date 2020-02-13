// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

#include "platform.h"
#include <interoplib.h>
#include <interoplibimports.h>

#include "comwrappers.h"

using OBJECTHANDLE = InteropLib::OBJECTHANDLE;
using RuntimeCallContext = InteropLibImports::RuntimeCallContext;

namespace InteropLib
{
    void Shutdown() noexcept
    {
        TrackerObjectManager::OnShutdown();
    }

    // Exposed COM related API
    namespace Com
    {
        HRESULT CreateWrapperForObject(
            _In_ OBJECTHANDLE instance,
            _In_ INT32 vtableCount,
            _In_ void* vtablesRaw,
            _In_ enum CreateComInterfaceFlags flags,
            _Outptr_ IUnknown** wrapper) noexcept
        {
            _ASSERTE(instance != nullptr && wrapper != nullptr);

            // Validate the supplied vtable data is valid with a
            // reasonable count.
            if ((vtablesRaw == nullptr && vtableCount != 0) || vtableCount < 0)
                return E_INVALIDARG;

            HRESULT hr;

            // Convert input to appropriate types.
            auto vtables = static_cast<ABI::ComInterfaceEntry*>(vtablesRaw);

            ManagedObjectWrapper* mow;
            RETURN_IF_FAILED(ManagedObjectWrapper::Create(flags, instance, vtableCount, vtables, &mow));

            *wrapper = static_cast<IUnknown*>(mow->As(IID_IUnknown));
            return S_OK;
        }

        void DestroyWrapperForObject(_In_ void* wrapperMaybe) noexcept
        {
            ManagedObjectWrapper* wrapper = ManagedObjectWrapper::MapFromIUnknown(static_cast<IUnknown*>(wrapperMaybe));

            // A caller should not be destroying a wrapper without knowing if the wrapper is valid.
            _ASSERTE(wrapper != nullptr);

            ManagedObjectWrapper::Destroy(wrapper);
        }

        HRESULT CreateWrapperForExternal(
            _In_ IUnknown* external,
            _In_ enum CreateObjectFlags flags,
            _In_ size_t contextSize,
            _Out_ ExternalWrapperResult* result) noexcept
        {
            _ASSERTE(external != nullptr && result != nullptr);

            HRESULT hr;

            // Attempt to create an agile reference first.
            ComHolder<IAgileReference> reference;
            RETURN_IF_FAILED(CreateAgileReference(external, &reference));

            NativeObjectWrapperContext* wrapperContext;
            RETURN_IF_FAILED(NativeObjectWrapperContext::Create(external, flags, contextSize, &wrapperContext));

            result->Context = wrapperContext->GetRuntimeContext();
            result->AgileRef = reference.Detach();
            result->FromTrackerRuntime = (wrapperContext->GetReferenceTracker() != nullptr);
            return S_OK;
        }

        void DestroyWrapperForExternal(_In_ void* contextMaybe) noexcept
        {
            NativeObjectWrapperContext* context = NativeObjectWrapperContext::MapFromRuntimeContext(contextMaybe);

            // A caller should not be destroying a context without knowing if the context is valid.
            _ASSERTE(context != nullptr);

            // Check if the tracker object manager should be informed prior to being destroyed.
            IReferenceTracker* trackerMaybe = context->GetReferenceTracker();
            if (trackerMaybe != nullptr)
            {
                // We only call this during a GC so ignore the failure as
                // there is no way we can handle it at this point.
                HRESULT hr = TrackerObjectManager::BeforeWrapperDestroyed(trackerMaybe);
                _ASSERTE(SUCCEEDED(hr));
                (void)hr;
            }

            NativeObjectWrapperContext::Destroy(context);
        }

        void SeparateWrapperFromTrackerRuntime(_In_ void* contextMaybe) noexcept
        {
            NativeObjectWrapperContext* context = NativeObjectWrapperContext::MapFromRuntimeContext(contextMaybe);

            // A caller should not be separating a context without knowing if the context is valid.
            _ASSERTE(context != nullptr);

            context->DisconnectTracker();
        }

        void GetIUnknownImpl(
            _Out_ void** fpQueryInterface,
            _Out_ void** fpAddRef,
            _Out_ void** fpRelease) noexcept
        {
            ManagedObjectWrapper::GetIUnknownImpl(fpQueryInterface, fpAddRef, fpRelease);
        }

        HRESULT EnsureActiveWrapperAndAddRef(_In_ IUnknown* wrapperMaybe, _In_ OBJECTHANDLE handle) noexcept
        {
            ManagedObjectWrapper* wrapper = ManagedObjectWrapper::MapFromIUnknown(wrapperMaybe);
            if (wrapper == nullptr || handle == nullptr)
                return E_INVALIDARG;

            ULONG count = wrapper->AddRef();
            if (count == 1)
            {
                ::InterlockedExchangePointer(&wrapper->Target, handle);
            }
            else
            {
                InteropLibImports::DeleteObjectInstanceHandle(handle);
            }

            return S_OK;
        }

        HRESULT BeginExternalObjectReferenceTracking(_In_ RuntimeCallContext* cxt) noexcept
        {
            return TrackerObjectManager::BeginReferenceTracking(cxt);
        }

        HRESULT EndExternalObjectReferenceTracking() noexcept
        {
            return TrackerObjectManager::EndReferenceTracking();
        }
    }
}
