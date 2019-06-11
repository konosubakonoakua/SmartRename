#include "stdafx.h"
#include "SmartRenameModel.h"

IFACEMETHODIMP_(ULONG) CSmartRenameModel::AddRef()
{
    return InterlockedIncrement(&m_refCount);
}

IFACEMETHODIMP_(ULONG) CSmartRenameModel::Release()
{
    long refCount = InterlockedDecrement(&m_refCount);

    if (refCount == 0)
    {
        delete this;
    }
    return refCount;
}

IFACEMETHODIMP CSmartRenameModel::QueryInterface(_In_ REFIID riid, _Outptr_ void** ppv)
{
    static const QITAB qit[] = {
        QITABENT(CSmartRenameModel, ISmartRenameModel),
        QITABENT(CSmartRenameModel, ISmartRenameRegExEvents),
        { 0 }
    };
    return QISearch(this, qit, riid, ppv);
}

IFACEMETHODIMP CSmartRenameModel::Advise(ISmartRenameModelEvents* renameOpEvents, DWORD* cookie)
{
    CSRWExclusiveAutoLock lock(&m_lockEvents);
    m_cookie++;
    SMART_RENAME_MODEL_EVENT srme;
    srme.cookie = m_cookie;
    srme.pEvents = renameOpEvents;
    renameOpEvents->AddRef();
    m_smartRenameModelEvents.push_back(srme);

    *cookie = m_cookie;

    return S_OK;
}

IFACEMETHODIMP CSmartRenameModel::UnAdvise(DWORD cookie)
{
    HRESULT hr = E_FAIL;
    CSRWExclusiveAutoLock lock(&m_lockEvents);

    for (std::vector<SMART_RENAME_MODEL_EVENT>::iterator it = m_smartRenameModelEvents.begin(); it != m_smartRenameModelEvents.end(); ++it)
    {
        if (it->cookie == cookie)
        {
            hr = S_OK;
            it->cookie = 0;
            if (it->pEvents)
            {
                it->pEvents->Release();
                it->pEvents = nullptr;
            }
            break;
        }
    }

    return hr;
}

IFACEMETHODIMP CSmartRenameModel::Start()
{
    return E_NOTIMPL;
}

IFACEMETHODIMP CSmartRenameModel::Stop()
{
    return E_NOTIMPL;
}

IFACEMETHODIMP CSmartRenameModel::Reset()
{
    return E_NOTIMPL;
}

IFACEMETHODIMP CSmartRenameModel::Shutdown()
{
    return E_NOTIMPL;
}

IFACEMETHODIMP CSmartRenameModel::AddItem(_In_ ISmartRenameItem* pItem)
{
    // Scope lock
    {
        CSRWExclusiveAutoLock lock(&m_lockItems);
        m_smartRenameItems.push_back(pItem);
        pItem->AddRef();
    }

    _OnItemAdded(pItem);

    return S_OK;
}

IFACEMETHODIMP CSmartRenameModel::GetItem(_In_ UINT index, _COM_Outptr_ ISmartRenameItem** ppItem)
{
    *ppItem = nullptr;
    CSRWSharedAutoLock lock(&m_lockItems);
    HRESULT hr = E_FAIL;
    if (index < m_smartRenameItems.size())
    {
        *ppItem = m_smartRenameItems.at(index);
        (*ppItem)->AddRef();
        hr = S_OK;
    }

    return hr;
}

IFACEMETHODIMP CSmartRenameModel::GetItemCount(_Out_ UINT* count)
{
    CSRWSharedAutoLock lock(&m_lockItems);
    *count = static_cast<UINT>(m_smartRenameItems.size());
    return S_OK;
}

IFACEMETHODIMP CSmartRenameModel::get_smartRenameRegEx(_COM_Outptr_ ISmartRenameRegEx** ppRegEx)
{
    HRESULT hr = E_FAIL;
    if (m_spRegEx)
    {
        hr = S_OK;
        *ppRegEx = m_spRegEx;
        (*ppRegEx)->AddRef();
    }
    return hr;
}

IFACEMETHODIMP CSmartRenameModel::put_smartRenameRegEx(_COM_Outptr_ ISmartRenameRegEx* pRegEx)
{
    m_spRegEx = pRegEx;
    return S_OK;
}

IFACEMETHODIMP CSmartRenameModel::get_smartRenameItemFactory(_In_ ISmartRenameItemFactory** ppItemFactory)
{
    HRESULT hr = E_FAIL;
    if (m_spItemFactory)
    {
        hr = S_OK;
        *ppItemFactory = m_spItemFactory;
        (*ppItemFactory)->AddRef();
    }
    return hr;
}

IFACEMETHODIMP CSmartRenameModel::put_smartRenameItemFactory(_In_ ISmartRenameItemFactory* pItemFactory)
{
    m_spItemFactory = pItemFactory;
    return S_OK;
}

IFACEMETHODIMP CSmartRenameModel::OnSearchTermChanged(_In_ PCWSTR /*searchTerm*/)
{
    // TODO: Cancel and restart rename preview thread
    return S_OK;
}

IFACEMETHODIMP CSmartRenameModel::OnReplaceTermChanged(_In_ PCWSTR /*replaceTerm*/)
{
    // TODO: Cancel and restart rename preview thread
    return S_OK;
}

IFACEMETHODIMP CSmartRenameModel::OnFlagsChanged(_In_ DWORD /*flags*/)
{
    // TODO: Cancel and restart rename preview thread
    return S_OK;
}

HRESULT CSmartRenameModel::s_CreateInstance(_COM_Outptr_ ISmartRenameModel** pprm)
{
    *pprm = nullptr;
    CSmartRenameModel *prm = new CSmartRenameModel();
    HRESULT hr = prm ? S_OK : E_OUTOFMEMORY;
    if (SUCCEEDED(hr))
    {
        hr = prm->QueryInterface(IID_PPV_ARGS(pprm));
        prm->Release();
    }
    return hr;
}

CSmartRenameModel::CSmartRenameModel() :
    m_refCount(1)
{
    m_startFileOpWorkerEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    m_startRegExWorkerEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    m_cancelRegExWorkerEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
}

CSmartRenameModel::~CSmartRenameModel()
{
}

DWORD WINAPI CSmartRenameModel::s_regexWorkerThread(void* pvoid)
{
    (pvoid);
    return 0;
}

void CSmartRenameModel::_Cancel()
{
    SetEvent(m_startFileOpWorkerEvent);
    SetEvent(m_startRegExWorkerEvent);
    SetEvent(m_cancelRegExWorkerEvent);
}

void CSmartRenameModel::_OnItemAdded(_In_ ISmartRenameItem* renameItem)
{
    CSRWExclusiveAutoLock lock(&m_lockEvents);

    for (std::vector<SMART_RENAME_MODEL_EVENT>::iterator it = m_smartRenameModelEvents.begin(); it != m_smartRenameModelEvents.end(); ++it)
    {
        if (it->pEvents)
        {
            it->pEvents->OnItemAdded(renameItem);
        }
    }
}

void CSmartRenameModel::_OnUpdate(_In_ ISmartRenameItem* renameItem)
{
    CSRWExclusiveAutoLock lock(&m_lockEvents);

    for (std::vector<SMART_RENAME_MODEL_EVENT>::iterator it = m_smartRenameModelEvents.begin(); it != m_smartRenameModelEvents.end(); ++it)
    {
        if (it->pEvents)
        {
            it->pEvents->OnUpdate(renameItem);
        }
    }
}

void CSmartRenameModel::_OnError(_In_ ISmartRenameItem* renameItem)
{
    CSRWExclusiveAutoLock lock(&m_lockEvents);

    for (std::vector<SMART_RENAME_MODEL_EVENT>::iterator it = m_smartRenameModelEvents.begin(); it != m_smartRenameModelEvents.end(); ++it)
    {
        if (it->pEvents)
        {
            it->pEvents->OnError(renameItem);
        }
    }
}

void CSmartRenameModel::_OnRegExStarted()
{
    CSRWExclusiveAutoLock lock(&m_lockEvents);

    for (std::vector<SMART_RENAME_MODEL_EVENT>::iterator it = m_smartRenameModelEvents.begin(); it != m_smartRenameModelEvents.end(); ++it)
    {
        if (it->pEvents)
        {
            it->pEvents->OnRegExStarted();
        }
    }
}

void CSmartRenameModel::_OnRegExCanceled()
{
    CSRWExclusiveAutoLock lock(&m_lockEvents);

    for (std::vector<SMART_RENAME_MODEL_EVENT>::iterator it = m_smartRenameModelEvents.begin(); it != m_smartRenameModelEvents.end(); ++it)
    {
        if (it->pEvents)
        {
            it->pEvents->OnRegExCanceled();
        }
    }
}

void CSmartRenameModel::_OnRegExCompleted()
{
    CSRWExclusiveAutoLock lock(&m_lockEvents);

    for (std::vector<SMART_RENAME_MODEL_EVENT>::iterator it = m_smartRenameModelEvents.begin(); it != m_smartRenameModelEvents.end(); ++it)
    {
        if (it->pEvents)
        {
            it->pEvents->OnRegExCompleted();
        }
    }
}

void CSmartRenameModel::_OnRenameStarted()
{
    CSRWExclusiveAutoLock lock(&m_lockEvents);

    for (std::vector<SMART_RENAME_MODEL_EVENT>::iterator it = m_smartRenameModelEvents.begin(); it != m_smartRenameModelEvents.end(); ++it)
    {
        if (it->pEvents)
        {
            it->pEvents->OnRenameStarted();
        }
    }
}

void CSmartRenameModel::_OnRenameCompleted()
{
    CSRWExclusiveAutoLock lock(&m_lockEvents);

    for (std::vector<SMART_RENAME_MODEL_EVENT>::iterator it = m_smartRenameModelEvents.begin(); it != m_smartRenameModelEvents.end(); ++it)
    {
        if (it->pEvents)
        {
            it->pEvents->OnRenameCompleted();
        }
    }
}

