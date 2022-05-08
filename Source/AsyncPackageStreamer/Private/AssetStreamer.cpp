// Copyright (c) 2014 Moritz Wundke & Ruben Avalos Elvira

#include "AssetStreamer.h"
#include "AsyncPackageStreamerPrivatePCH.h"
#include "IPlatformFilePak.h"
#include "StreamingNetworkPlatformFile.h"
#include "HAL/FileManagerGeneric.h"

FAssetStreamer::FAssetStreamer()
    : CurrentMode(), LockValue(0), LocalPlatformFile(nullptr), NetPlatform(new FStreamingNetworkPlatformFile())
      , PakPlatform(nullptr), Listener(nullptr), bSigned(false), StreamableManager(nullptr)
{
    Initialize();
}

FAssetStreamer::~FAssetStreamer()
{
    if(bInitialized)
    {
        OnStreamingCompleteDelegate();
    }
    //if (StreamableManager) delete StreamableManager;
    Unlock();
}

bool FAssetStreamer::Initialize()
{
    if (!bInitialized)
    {
        PakPlatform = MakeShareable<FPakPlatformFile>(new FPakPlatformFile());
        LocalPlatformFile = &FPlatformFileManager::Get().GetPlatformFile();
        //PakPlatform->Initialize(LocalPlatformFile, TEXT(""));
        //FPlatformFileManager::Get().SetPlatformFile(*PakPlatform);
        StreamableManager = MakeShareable<FStreamableManager>(new FStreamableManager());
        
        // Load config values
        if (!GConfig->GetString(TEXT("AssetStreamer"), TEXT("ServerHost"), ServerHost, GEngineIni))
        {
            ServerHost = TEXT("127.0.0.1:8081");
        }
        if (!GConfig->GetBool(TEXT("AssetStreamer"), TEXT("bSigned"), bSigned, GEngineIni))
        {
            bSigned = false;
        }
        bInitialized = true;

        /*if (LocalPlatformFile != nullptr)
        {
            IPlatformFile* PakPlatformFile = FPlatformFileManager::Get().GetPlatformFile(TEXT("PakFile"));
            IPlatformFile* NetPlatformFile = FPlatformFileManager::Get().GetPlatformFile(TEXT("NetworkFile"));
            if (PakPlatformFile != nullptr && NetPlatformFile != nullptr)
            {
                if (NetPlatform.IsValid())
                {
                    bInitialized = NetPlatform->Initialize(LocalPlatformFile, *FString::Printf(TEXT("-FileHostIP=%s"), *ServerHost));
                }
            }
            bInitialized = true;
        }*/
    }

    if (!bInitialized)
    {
        UE_LOG(LogAsyncPackageStreamer, Error, TEXT("Failed to initialize AssetStreamer using %s for remote file streaming"), *ServerHost);
    }
    

    return bInitialized;
}

bool FAssetStreamer::StreamPackage(const FString& PakFileName, TSharedPtr<IAssetStreamerListener> AssetStreamerListener, EAssetStreamingMode::Type DesiredMode, const TCHAR* CmdLine)
{
    bool retval = LoadPakAndTryStream(DesiredMode, CmdLine, PakFileName);
    if (!retval) return false;

    // Once we started the async work assign listener
    Listener = AssetStreamerListener;

    // Tell the listener which assets we are about to stream
    if (Listener.Get())
    {
        Listener.Get()->OnPrepareAssetStreaming(StreamedAssets);
    }

    // IF we have not yet a StreamableManager setup (Arrr...) abort
    if (StreamableManager.GetSharedReferenceCount()==0)
    {
        Unlock();
        UE_LOG(LogAsyncPackageStreamer, Error, TEXT("No StreamableManager registered, did you missed to call initialize?"));
        return false;
    }

    //StreamableManager->RequestAsyncLoad(StreamedAssets, FStreamableDelegate::CreateRaw(this, &FAssetStreamer::OnStreamingCompleteDelegate));
    return true;
}

bool FAssetStreamer::StreamPackage(const FString& PakFileName, IAssetStreamerListener* AssetStreamerListener, EAssetStreamingMode::Type DesiredMode, const TCHAR* CmdLine)
{
    bool retval = LoadPakAndTryStream(DesiredMode, CmdLine, PakFileName);
    if (!retval) return false;

    // Once we started the async work assign listener
    ListenerObjPtr = AssetStreamerListener;

    // Tell the listener which assets we are about to stream
    if (ListenerObjPtr)
    {
        ListenerObjPtr->OnPrepareAssetStreaming(StreamedAssets);
    }

    // IF we have not yet a StreamableManager setup (Arrr...) abort
    if (StreamableManager.GetSharedReferenceCount() == 0)
    {
        Unlock();
        UE_LOG(LogAsyncPackageStreamer, Error, TEXT("No StreamableManager registered, did you missed to call initialize?"));
        return false;
    }

    //StreamableManager->RequestAsyncLoad(StreamedAssets, FStreamableDelegate::CreateRaw(this, &FAssetStreamer::OnStreamingCompleteDelegate));
    return true;
}

bool FAssetStreamer::LoadPakAndTryStream(EAssetStreamingMode::Type DesiredMode, const TCHAR* CmdLine, const FString& PakFileName)
{
    Lock();
    Listener = NULL;

    const bool bRemote = (DesiredMode == EAssetStreamingMode::Remote);
    if (!((bRemote && UseRemote(CmdLine)) || (!bRemote && UseLocal(CmdLine))))
    {
        Unlock();
        return false;
    }
    CurrentMode = DesiredMode;
    FPlatformFileManager::Get().SetPlatformFile(*PakPlatform);

    // Now Get the path and start the streaming
    const FString FilePath = bRemote ? ResolveRemotePath(PakFileName) : ResolveLocalPath(PakFileName);
    if (!FPaths::FileExists(FilePath))
    {
        Unlock();
        UE_LOG(LogAsyncPackageStreamer, Error, TEXT("Invalid pak file path: %s"), *FilePath);
        return false;
    }
    // Make sure the Pak file is actually there
    TRefCountPtr<FPakFile> PakFilePtr = new FPakFile(PakPlatform.Get(), *FilePath, bSigned, true);
    FPakFile& PakFile = *PakFilePtr;
    if (!PakFile.IsValid())
    {
        Unlock();
        UE_LOG(LogAsyncPackageStreamer, Error, TEXT("Invalid pak file: %s"), *FilePath);
        return false;
    }

    // TODO: Do we need to mount it into the engine dir? Create a DLC dir instead?
    PakFile.SetMountPoint(*FPaths::EngineContentDir());
    const int32 PakOrder = 0;
    if (!PakPlatform->Mount(*FilePath, PakOrder, *FPaths::EngineContentDir()))
    {
        Unlock();
        UE_LOG(LogAsyncPackageStreamer, Error, TEXT("Failed to mount pak file: %s"), *FilePath);
        return false;
    }

    // Load all assets contained in this Pak file
    TSet<FString> FileList;
    PakFile.FindFilesAtPath(FileList, *PakFile.GetMountPoint(), true, false, true);
    //PakFile.FindPrunedFilesAtPath(FileList, *PakFile.GetMountPoint(), true, false, true);
    UE_LOG(LogAsyncPackageStreamer, Warning, TEXT("FILELIST COUNT: %d"), FileList.Num());


    // Discover assets within the PakFile
    StreamedAssets.Empty();
    for (TSet<FString>::TConstIterator FileItem(FileList); FileItem; ++FileItem)
    {
        FSoftObjectPath AssetName = *FileItem;
        FString AssetNameString = AssetName.ToString();

        UE_LOG(LogAsyncPackageStreamer, Warning, TEXT("PROCESSING ASSET: %s"), *AssetNameString);

        if (AssetNameString.EndsWith(FPackageName::GetAssetPackageExtension()) ||
            AssetNameString.EndsWith(FPackageName::GetMapPackageExtension()))
        {
            //AssetNameString = AssetNameString.Replace(L".umap", L".AdditionalLevel").Replace(L"../../../Engine/Content/Plugins", L"/Engine/Content/Plugins");
            // TODO: Set path relative to mountpoint for remote streaming?
            //StreamedAssets.Add(AssetName);
            //{AssetPathName=0x0000024d0e423d30 "../../../Engine/Content/Plugins/MyDLC/Content/AdditionalLevel.umap" ...}
            StreamedAssets.Add(
                //AssetNameString.Replace(L"../../../Engine/Content/Plugins", L"")
                AssetNameString
            );

            UE_LOG(LogAsyncPackageStreamer, Warning, TEXT("Adding asset to StreamedAssets: %s"), *AssetNameString);

        }
    }
    return true;
}


void FAssetStreamer::BlockUntilStreamingFinished(float SleepTime) const
{
    while (LockValue != 0)
    {
        FPlatformProcess::Sleep(SleepTime);
    }
}

bool FAssetStreamer::UseRemote(const TCHAR* CmdLine)
{
    if (bInitialized && PakPlatform.IsValid())
    {
        IPlatformFile* InnerPlatform = NetPlatform.Get();
        return InnerPlatform && PakPlatform->Initialize(InnerPlatform, CmdLine);
    }
    UE_LOG(LogAsyncPackageStreamer, Error, TEXT("Failed to initialize remote file system! you wont be able to load PAK files!"));
    return false;
}


bool FAssetStreamer::UseLocal(const TCHAR* CmdLine)
{
    if (bInitialized && PakPlatform.IsValid())
    {
        IPlatformFile* InnerPlatform = LocalPlatformFile;
        return InnerPlatform && PakPlatform->Initialize(InnerPlatform, CmdLine);
    }
    UE_LOG(LogAsyncPackageStreamer, Error, TEXT("Failed to initialize local file system! you wont be able to load PAK files!"));
    return false;
}

/** Resolve a remote path to a given hosted PAK file */
FString FAssetStreamer::ResolveRemotePath(const FString& FileName)
{
    // TODO: We need to setup where the remote host will host the files based on the root of the file server!
    return FileName + TEXT(".pak");
}

/** Resolves a local path to a given PAK file */
FString FAssetStreamer::ResolveLocalPath(const FString& FileName)
{
    return FileName + TEXT(".pak");
}

void FAssetStreamer::OnStreamingCompleteDelegate()
{
    // Call listener!
    if (Listener.Get() != nullptr)
    {
       // Listener.Get()->OnAssetStreamComplete();
        //Listener = nullptr;
    }

    // Finally unlock the streamer
    Unlock();
}