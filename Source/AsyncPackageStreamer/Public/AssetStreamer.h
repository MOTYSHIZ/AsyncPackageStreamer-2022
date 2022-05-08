// Copyright (c) 2014 Moritz Wundke & Ruben Avalos Elvira

#pragma once

class IPlatformFile;
class FStreamingNetworkPlatformFile;
class FPakPlatformFile;
struct FSoftObjectPath;
struct FStreamableManager;

namespace EAssetStreamingMode
{
    enum Type
    {
        Local,
        Remote
    };
}

/**
* Interface
*/
class ASYNCPACKAGESTREAMER_API IAssetStreamerListener
{
public:
    /** Must have the same signature as FStreamableDelegate. Called when the streaming has completed. */
    virtual void OnAssetStreamComplete()
    {
        UE_LOG(LogTemp, Log, TEXT("Assets Stream Completed"));
    }

    /** Called before the streaming is initiated, used to inform which assets will be requested */
    virtual void OnPrepareAssetStreaming(const TArray<FSoftObjectPath>& StreamedAssets)
    {
        
    }
    virtual ~IAssetStreamerListener(){}
};


/**
 * The asset streamer is capable of loading and mounting the content of a PAK file from the local FS
 * or a remote one.
 */
class ASYNCPACKAGESTREAMER_API FAssetStreamer
{
   
public:
    FAssetStreamer();

    /** Unlock the streamer when destroyed */
    ~FAssetStreamer();


    /**
     * Stream assets form a PAK file remotely or from the local file system
     */
    bool StreamPackage(const FString& PakFileName, TSharedPtr<IAssetStreamerListener> AssetStreamerListener, EAssetStreamingMode::Type DesiredMode, const TCHAR* CmdLine);
    // Version for use with UObjects
    bool StreamPackage(const FString& PakFileName, IAssetStreamerListener* AssetStreamerListener, EAssetStreamingMode::Type DesiredMode, const TCHAR* CmdLine);

    bool LoadPakAndTryStream(EAssetStreamingMode::Type DesiredMode, const TCHAR* CmdLine, const FString& PakFileName);

    /** Check if the streamed has been initialized or not */
    bool bInitialized = false;

    /** The mode that is currently set for streaming */
    EAssetStreamingMode::Type CurrentMode;

    bool IsStreaming() const
    {
        return LockValue != 0;
    }

    /** Blocks this thread until we have stopped streaming */
    void BlockUntilStreamingFinished(float SleepTime = 0.1f) const;

    /** The host:port connection string for the remote file server*/
    FString ServerHost;

private:
    /** Initialize the FAssetStreamer and point to the using host:port */
    bool Initialize();

    /** Set the  streaming to be using the remote FS*/
    bool UseRemote(const TCHAR* CmdLine);

    /** Set the  streaming to be using the local FS*/
    bool UseLocal(const TCHAR* CmdLine);

    /** Resolve a remote path to a given hosted PAK file */
    FString ResolveRemotePath(const FString& FileName);

    /** Resolves a local path to a given PAK file */
    FString ResolveLocalPath(const FString& FileName);

    /** Locks the primitive so no one can proceed past a block point */
    void Lock()
    {
        FPlatformAtomics::InterlockedExchange(&LockValue, 1);
    }

    /** Unlocks the primitive so other threads can proceed past a block point */
    void Unlock()
    {
        FPlatformAtomics::InterlockedExchange(&LockValue, 0);
    }

    /** Called once streaming has finished, will call our listener and unlock the streamer again */
    void OnStreamingCompleteDelegate();

    /** Thread safe lock value */
    volatile int32 LockValue;

    // The platform file instances that will help us perform local and remote loading
    IPlatformFile* LocalPlatformFile;
    TSharedPtr<FStreamingNetworkPlatformFile> NetPlatform;
    TSharedPtr<FPakPlatformFile> PakPlatform;

    /** The current listener for the current streaming task */
    TSharedPtr<IAssetStreamerListener> Listener;

    /** The current listener for the current streaming task */
    IAssetStreamerListener* ListenerObjPtr;

    /** The assets that we are goind to stream */
    TArray<FSoftObjectPath> StreamedAssets;

    /** Should we use signed PAKs only? */
    bool bSigned;

    /** The streamable manager 'someone' must provide us with from the outside */
    TSharedPtr<FStreamableManager> StreamableManager;

};