// Copyright (c) 2014 Moritz Wundke & Ruben Avalos Elvira

#pragma once

#include "Modules/ModuleManager.h"
#include "AssetStreamer.h"

struct FStreamableManager;

/**
 * The public interface to this module.  In most cases, this interface is only public to sibling modules 
 * within this plugin.
 */
class IAsyncPackageStreamer : public IModuleInterface
{

public:

    /**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static inline IAsyncPackageStreamer& Get()
	{
		return FModuleManager::LoadModuleChecked< IAsyncPackageStreamer >( "AsyncPackageStreamer" );
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded( "AsyncPackageStreamer" );
	}
};
