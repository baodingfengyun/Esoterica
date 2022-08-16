#include "ResourceServer.h"
#include "_AutoGenerated/ToolsTypeRegistration.h"
#include "EngineTools/Resource/ResourceCompiler.h"
#include "Engine/Entity/EntityDescriptors.h"
#include "Engine/Entity/EntitySerialization.h"
#include "System/Resource/ResourceProviders/ResourceNetworkMessages.h"
#include "System/IniFile.h"
#include "System/FileSystem/FileSystem.h"
#include "System/FileSystem/FileSystemUtils.h"

#include <sstream>

//-------------------------------------------------------------------------

namespace EE::Resource
{
    ResourceServer::ResourceServer()
    {
        m_activeRequests.reserve( 100 );
        m_maxSimultaneousCompilationTasks = Threading::GetProcessorInfo().m_numPhysicalCores;
    }

    ResourceServer::~ResourceServer()
    {
        EE_ASSERT( m_pCompilerRegistry == nullptr );
    }

    bool ResourceServer::Initialize( IniFile const& iniFile )
    {
        EE_ASSERT( iniFile.IsValid() );

        if ( !m_settings.ReadSettings( iniFile ) )
        {
            return false;
        }

        // Register types
        //-------------------------------------------------------------------------

        AutoGenerated::Tools::RegisterTypes( m_typeRegistry );

        m_pCompilerRegistry = EE::New<CompilerRegistry>( m_typeRegistry, m_settings.m_rawResourcePath );

        // Connect to compiled resource database
        //-------------------------------------------------------------------------

        if ( !m_compiledResourceDatabase.TryConnect( m_settings.m_compiledResourceDatabasePath ) )
        {
            m_errorMessage.sprintf( "Database connection error: %s", m_compiledResourceDatabase.GetError().c_str() );
            return false;
        }

        // Open network connection
        //-------------------------------------------------------------------------

        if ( !Network::NetworkSystem::Initialize() )
        {
            return false;
        }

        if ( !Network::NetworkSystem::StartServerConnection( &m_networkServer, m_settings.m_resourceServerPort ) )
        {
            return false;
        }

        //-------------------------------------------------------------------------

        if ( m_fileSystemWatcher.StartWatching( m_settings.m_rawResourcePath ) )
        {
            m_fileSystemWatcher.RegisterChangeListener( this );
        }

        // Create Workers
        //-------------------------------------------------------------------------

        m_taskSystem.Initialize();

        for ( auto i = 0u; i < m_maxSimultaneousCompilationTasks; i++ )
        {
            m_workers.emplace_back( EE::New<ResourceServerWorker>( &m_taskSystem, m_settings.m_resourceCompilerExecutablePath.c_str() ) );
        }

        // Packaging
        //-------------------------------------------------------------------------

        RefreshAvailableMapList();

        return true;
    }

    void ResourceServer::Shutdown()
    {
        // Destroy workers
        //-------------------------------------------------------------------------

        m_taskSystem.WaitForAll();

        for ( auto& pWorker : m_workers )
        {
            EE::Delete( pWorker );
        }

        m_workers.clear();

        m_taskSystem.Shutdown();

        // Unregister File Watcher
        //-------------------------------------------------------------------------

        if ( m_fileSystemWatcher.IsWatching() )
        {
            m_fileSystemWatcher.StopWatching();
            m_fileSystemWatcher.UnregisterChangeListener( this );
        }

        // Delete requests
        //-------------------------------------------------------------------------

        for ( auto& pRequest : m_pendingRequests )
        {
            EE::Delete( pRequest );
        }

        for ( auto& pRequest : m_activeRequests )
        {
            EE::Delete( pRequest );
        }

        CleanupCompletedRequests();

        //-------------------------------------------------------------------------

        Network::NetworkSystem::StopServerConnection( &m_networkServer );
        Network::NetworkSystem::Shutdown();

        //-------------------------------------------------------------------------

        EE::Delete( m_pCompilerRegistry );

        AutoGenerated::Tools::UnregisterTypes( m_typeRegistry );
    }

    //-------------------------------------------------------------------------

    void ResourceServer::Update()
    {
        // Update network server
        //-------------------------------------------------------------------------

        Network::NetworkSystem::Update();

        if ( m_networkServer.IsRunning() )
        {
            auto ProcessIncomingMessages = [this] ( Network::IPC::Message const& message )
            {
                if ( message.GetMessageID() == (int32_t) NetworkMessageID::RequestResource )
                {
                    uint32_t const clientID = message.GetClientConnectionID();
                    NetworkResourceRequest networkRequest = message.GetData<NetworkResourceRequest>();
                    CreateResourceRequest( networkRequest.m_path, clientID );
                }
            };

            m_networkServer.ProcessIncomingMessages( ProcessIncomingMessages );
        }

        // Update requests
        //-------------------------------------------------------------------------

        // Check status of active requests
        for ( auto pWorker : m_workers )
        {
            if ( pWorker->IsComplete() )
            {
                auto const pCompletedRequest = pWorker->AcceptResult();

                // Update database
                //-------------------------------------------------------------------------

                if ( pCompletedRequest->HasSucceeded() )
                {
                    WriteCompiledResourceRecord( pCompletedRequest );
                }

                // Send network response
                //-------------------------------------------------------------------------

                NotifyClientOnCompletedRequest( pCompletedRequest );

                // Remove from active list
                //-------------------------------------------------------------------------

                m_completedRequests.emplace_back( pCompletedRequest );
                m_activeRequests.erase_first_unsorted( pCompletedRequest );
            }
        }

        // Kick off new requests
        while ( m_pendingRequests.size() > 0 && m_activeRequests.size() < m_maxSimultaneousCompilationTasks )
        {
            auto pRequestToStart = m_pendingRequests[0];
            m_activeRequests.emplace_back( pRequestToStart );
            m_pendingRequests.erase( m_pendingRequests.begin() );

            bool taskStarted = false;
            for ( auto& pWorker : m_workers )
            {
                if ( pWorker->IsIdle() )
                {
                    pWorker->Compile( pRequestToStart );
                    taskStarted = true;
                    break;
                }
            }

            EE_ASSERT( taskStarted );
        }

        // Process cleanup request
        //-------------------------------------------------------------------------

        if ( m_cleanupRequested )
        {
            CleanupCompletedRequests();
            m_cleanupRequested = false;
        }

        // Update File System Watcher
        //-------------------------------------------------------------------------

        if ( m_fileSystemWatcher.IsWatching() )
        {
            m_fileSystemWatcher.Update();
        }

        // Packaging
        //-------------------------------------------------------------------------

        if ( m_isPackaging )
        {
            if ( m_completedPackagingRequests.size() == m_resourcesToBePackaged.size() )
            {
                m_resourcesToBePackaged.clear();
                m_completedPackagingRequests.clear();
                m_isPackaging = false;
            }
        }

        // Reset Counter
        //-------------------------------------------------------------------------

        size_t const totalActiveAndPendingRequests = m_pendingRequests.size() + m_activeRequests.size();
        if ( totalActiveAndPendingRequests == 0 )
        {
            m_numRequestedResources = 0;
        }
    }

    ResourceServer::BusyState ResourceServer::GetBusyState() const
    {
        BusyState state;

        int32_t const totalActiveAndPendingRequests = int32_t( m_pendingRequests.size() + m_activeRequests.size() );
        if ( totalActiveAndPendingRequests > 0 )
        {
            state.m_completedRequests = m_numRequestedResources - totalActiveAndPendingRequests;
            state.m_totalRequests = m_numRequestedResources;
            state.m_isBusy = true;
        }
        else
        {
            state.m_isBusy = false;
        }

        //-------------------------------------------------------------------------

        if ( state.m_isBusy )
        {
            EE_ASSERT( state.m_totalRequests > 0 );
            EE_ASSERT( state.m_completedRequests <= state.m_totalRequests );
        }

        return state;
    }

    void ResourceServer::OnFileModified( FileSystem::Path const& filePath )
    {
        EE_ASSERT( filePath.IsValid() && filePath.IsFilePath() );

        ResourcePath resourcePath = ResourcePath::FromFileSystemPath( m_settings.m_rawResourcePath, filePath );
        if ( !resourcePath.IsValid() )
        {
            return;
        }

        ResourceID resourceID( resourcePath );
        if ( !resourceID.IsValid() )
        {
            return;
        }

        // Check compiled resources database for a record for this file
        auto compiledResourceRecord = m_compiledResourceDatabase.GetRecord( resourceID );
        if ( !compiledResourceRecord.IsValid() )
        {
            return;
        }

        // If we have a record, then schedule a recompile task
        CreateResourceRequest( resourceID, 0, CompilationRequest::Origin::FileWatcher );
    }

    void ResourceServer::CleanupCompletedRequests()
    {
        for ( int32_t i = (int32_t) m_completedRequests.size() - 1; i >= 0; i-- )
        {
            EE_ASSERT( !VectorContains( m_activeRequests, m_completedRequests[i] ) && !VectorContains( m_pendingRequests, m_completedRequests[i] ) );
            EE::Delete( m_completedRequests[i] );
            m_completedRequests.erase_unsorted( m_completedRequests.begin() + i );
        }
    }

    //-------------------------------------------------------------------------

    void ResourceServer::CreateResourceRequest( ResourceID const& resourceID, uint32_t clientID, CompilationRequest::Origin origin )
    {
        EE_ASSERT( m_compiledResourceDatabase.IsConnected() );

        CompilationRequest* pRequest = EE::New<CompilationRequest>();

        if ( resourceID.IsValid() )
        {
            if ( origin == CompilationRequest::Origin::External )
            {
                EE_ASSERT( clientID != 0 );
            }
            else
            {
                EE_ASSERT( clientID == 0 );
            }

            //-------------------------------------------------------------------------

            pRequest->m_clientID = clientID;
            pRequest->m_origin = origin;
            pRequest->m_resourceID = resourceID;
            pRequest->m_sourceFile = ResourcePath::ToFileSystemPath( m_settings.m_rawResourcePath, pRequest->m_resourceID.GetResourcePath() );
            pRequest->m_compilerArgs = pRequest->m_resourceID.GetResourcePath().c_str();

            // Set the destination path based on request type
            if ( origin == CompilationRequest::Origin::Package )
            {
                pRequest->m_destinationFile = ResourcePath::ToFileSystemPath( m_settings.m_packagedBuildCompiledResourcePath, pRequest->m_resourceID.GetResourcePath() );
            }
            else
            {
                pRequest->m_destinationFile = ResourcePath::ToFileSystemPath( m_settings.m_compiledResourcePath, pRequest->m_resourceID.GetResourcePath());
            }

            // Resource type validity check
            ResourceTypeID const resourceTypeID = pRequest->m_resourceID.GetResourceTypeID();
            if ( m_pCompilerRegistry->IsVirtualResourceType( resourceTypeID ) )
            {
                pRequest->m_log.sprintf( "Virtual Resource (%s) - Nothing to do!", pRequest->m_sourceFile.GetFullPath().c_str() );
                pRequest->m_status = CompilationRequest::Status::Succeeded;
            }
            else
            {
                auto pCompiler = m_pCompilerRegistry->GetCompilerForResourceType( resourceTypeID );
                if ( pCompiler == nullptr )
                {
                    pRequest->m_log.sprintf( "Error: No compiler found for resource type (%s)!", pRequest->m_resourceID.ToString().c_str() );
                    pRequest->m_status = CompilationRequest::Status::Failed;
                }

                // File Validity check
                bool sourceFileExists = false;
                if ( pRequest->m_status != CompilationRequest::Status::Failed )
                {
                    sourceFileExists = FileSystem::Exists( pRequest->m_sourceFile );
                    if ( pCompiler->IsInputFileRequired() && !sourceFileExists )
                    {
                        pRequest->m_log.sprintf( "Error: Source file (%s) doesnt exist!", pRequest->m_sourceFile.GetFullPath().c_str() );
                        pRequest->m_status = CompilationRequest::Status::Failed;
                    }
                }

                // Try create target dir
                if ( pRequest->m_status != CompilationRequest::Status::Failed )
                {
                    if ( !pRequest->m_destinationFile.EnsureDirectoryExists() )
                    {
                        pRequest->m_log.sprintf( "Error: Destination path (%s) doesnt exist!", pRequest->m_destinationFile.GetParentDirectory().c_str() );
                        pRequest->m_status = CompilationRequest::Status::Failed;
                    }
                }

                // Check that target file isnt read-only
                if ( pRequest->m_status != CompilationRequest::Status::Failed )
                {
                    if ( FileSystem::Exists( pRequest->m_destinationFile ) && FileSystem::IsFileReadOnly( pRequest->m_destinationFile ) )
                    {
                        pRequest->m_log.sprintf( "Error: Destination file (%s) is read-only!", pRequest->m_destinationFile.GetFullPath().c_str() );
                        pRequest->m_status = CompilationRequest::Status::Failed;
                    }
                }

                // Check compile dependencies
                TVector<ResourcePath> compileDependencies;
                if ( pRequest->m_status != CompilationRequest::Status::Failed && sourceFileExists )
                {
                    // Try to read all the resource compile dependencies for non-map resources
                    if ( pRequest->GetResourceID().GetResourceTypeID() != ResourceTypeID( "map" ) )
                    {
                        if ( !TryReadCompileDependencies( pRequest->m_sourceFile, compileDependencies, &pRequest->m_log ) )
                        {
                            pRequest->m_log += "Error: failed to read compile dependencies!";
                            pRequest->m_status = CompilationRequest::Status::Failed;
                        }
                    }
                }

                // Run Up-to-date check
                bool const forceRecompile = ( origin == CompilationRequest::Origin::ManualCompile );
                if ( pRequest->m_status != CompilationRequest::Status::Failed && !forceRecompile )
                {
                    PerformResourceUpToDateCheck( pRequest, compileDependencies );
                }
            }
        }
        else // Invalid resource ID
        {
            pRequest->m_log.sprintf( "Error: Invalid resource ID ( %s )", resourceID.c_str() );
            pRequest->m_status = CompilationRequest::Status::Failed;
        }

        // Enqueue new request
        //-------------------------------------------------------------------------

        if ( pRequest->IsPending() )
        {
            m_pendingRequests.emplace_back( pRequest );
        }
        else // Failed or Up-to-date
        {
            m_completedRequests.emplace_back( pRequest );
            EE_ASSERT( pRequest->IsComplete() );
            NotifyClientOnCompletedRequest( pRequest );
        }

        m_numRequestedResources++;
    }

    void ResourceServer::NotifyClientOnCompletedRequest( CompilationRequest* pRequest )
    {
        NetworkResourceResponse response;
        response.m_resourceID = pRequest->GetResourceID();
        if ( pRequest->HasSucceeded() )
        {
            response.m_filePath = pRequest->GetDestinationFilePath();
        }

        //-------------------------------------------------------------------------

        // Notify all clients
        if ( pRequest->IsInternalRequest() )
        {
            // Remove from list of resources being packaged since the request is complete
            if ( pRequest->m_origin == CompilationRequest::Origin::Package )
            {
                m_completedPackagingRequests.emplace_back( pRequest->m_resourceID );
            }

            // Bulk notify all connected client that a resource has been recompile so that they can reload it if necessary
            for ( auto const& clientInfo : m_networkServer.GetConnectedClients() )
            {
                Network::IPC::Message message;
                message.SetClientConnectionID( clientInfo.m_ID );
                message.SetData( (int32_t) NetworkMessageID::ResourceUpdated, response );
                m_networkServer.SendNetworkMessage( eastl::move( message ) );
            }
        }
        else // Notify single client
        {
            Network::IPC::Message message;
            message.SetClientConnectionID( pRequest->GetClientID() );
            message.SetData( (int32_t) NetworkMessageID::ResourceRequestComplete, response );
            m_networkServer.SendNetworkMessage( eastl::move( message ) );
        }
    }

    //-------------------------------------------------------------------------

    void ResourceServer::PerformResourceUpToDateCheck( CompilationRequest* pRequest, TVector<ResourcePath> const& compileDependencies ) const
    {
        EE_ASSERT( pRequest != nullptr && pRequest->IsPending() );

        pRequest->m_upToDateCheckTimeStarted = PlatformClock::GetTime();

        // Read all up to date information
        //-------------------------------------------------------------------------

        pRequest->m_compilerVersion = m_pCompilerRegistry->GetVersionForType( pRequest->m_resourceID.GetResourceTypeID() );
        EE_ASSERT( pRequest->m_compilerVersion >= 0 );

        pRequest->m_fileTimestamp = FileSystem::GetFileModifiedTime( pRequest->m_sourceFile );

        bool areCompileDependenciesAreUpToDate = true;
        for ( auto const& compileDep : compileDependencies )
        {
            EE_ASSERT( compileDep.IsValid() );

            ResourceTypeID const extension( compileDep.GetExtension() );
            if ( IsCompileableResourceType( extension ) && !IsResourceUpToDate( ResourceID( compileDep ) ) )
            {
                areCompileDependenciesAreUpToDate = false;
                break;
            }

            auto const compileDependencyPath = ResourcePath::ToFileSystemPath( m_settings.m_rawResourcePath, compileDep );
            if ( !FileSystem::Exists( compileDependencyPath ) )
            {
                areCompileDependenciesAreUpToDate = false;
                break;
            }

            pRequest->m_sourceTimestampHash += FileSystem::GetFileModifiedTime( compileDependencyPath );
        }

        // Check source file for changes
        //-------------------------------------------------------------------------

        // Check compile dependency state
        bool isResourceUpToDate = areCompileDependenciesAreUpToDate;

        // Check against previous compilation result
        if ( isResourceUpToDate )
        {
            auto existingRecord = m_compiledResourceDatabase.GetRecord( pRequest->m_resourceID );
            if ( existingRecord.IsValid() )
            {
                if ( pRequest->m_compilerVersion != existingRecord.m_compilerVersion )
                {
                    isResourceUpToDate = false;
                }

                if ( pRequest->m_fileTimestamp != existingRecord.m_fileTimestamp )
                {
                    isResourceUpToDate = false;
                }

                if ( pRequest->m_sourceTimestampHash != existingRecord.m_sourceTimestampHash )
                {
                    isResourceUpToDate = false;
                }
            }
            else
            {
                isResourceUpToDate = false;
            }
        }

        // Check that the target file exists
        if ( isResourceUpToDate && !FileSystem::Exists( pRequest->m_destinationFile ) )
        {
            isResourceUpToDate = false;
        }

        //-------------------------------------------------------------------------

        if ( isResourceUpToDate )
        {
            pRequest->m_log.sprintf( "Resource up to date! (%s)", pRequest->m_sourceFile.GetFullPath().c_str() );
            pRequest->m_status = CompilationRequest::Status::Succeeded;
        }

        //-------------------------------------------------------------------------

        pRequest->m_upToDateCheckTimeFinished = PlatformClock::GetTime();
    }

    bool ResourceServer::TryReadCompileDependencies( FileSystem::Path const& resourceFilePath, TVector<ResourcePath>& outDependencies, String* pErrorLog ) const
    {
        EE_ASSERT( resourceFilePath.IsValid() );

        // Read JSON descriptor file - we do this by hand since we dont want to create a type registry in the resource server
        if ( FileSystem::Exists( resourceFilePath ) )
        {
            FILE* fp = fopen( resourceFilePath, "r" );
            if ( fp == nullptr )
            {
                return false;
            }

            fseek( fp, 0, SEEK_END );
            size_t filesize = (size_t) ftell( fp );
            fseek( fp, 0, SEEK_SET );

            String fileContents;
            fileContents.resize( filesize );
            size_t const readLength = fread( fileContents.data(), 1, filesize, fp );
            fclose( fp );

            ResourceDescriptor::ReadCompileDependencies( fileContents, outDependencies );
        }
        else
        {
            return false;
        }

        //-------------------------------------------------------------------------

        for ( auto const& dep : outDependencies )
        {
            if ( !dep.IsValid() )
            {
                return false;
            }
        }

        return true;
    }

    bool ResourceServer::IsResourceUpToDate( ResourceID const& resourceID ) const
    {
        // Check that the target file exists
        //-------------------------------------------------------------------------

        if ( !FileSystem::Exists( ResourcePath::ToFileSystemPath( m_settings.m_compiledResourcePath, resourceID.GetResourcePath() ) ) )
        {
            return false;
        }

        // Check compile dependencies
        //-------------------------------------------------------------------------

        int32_t const compilerVersion = m_pCompilerRegistry->GetVersionForType( resourceID.GetResourceTypeID() );
        EE_ASSERT( compilerVersion >= 0 );

        FileSystem::Path const sourceFilePath = ResourcePath::ToFileSystemPath( m_settings.m_rawResourcePath, resourceID.GetResourcePath() );
        if ( !FileSystem::Exists( sourceFilePath ) )
        {
            return false;
        }

        uint64_t const fileTimestamp = FileSystem::GetFileModifiedTime( sourceFilePath );
        uint64_t sourceTimestampHash = 0;

        TVector<ResourcePath> compileDependencies;
        if ( !TryReadCompileDependencies( sourceFilePath, compileDependencies ) )
        {
            return false;
        }

        for ( auto const& compileDep : compileDependencies )
        {
            sourceTimestampHash += FileSystem::GetFileModifiedTime( ResourcePath::ToFileSystemPath( m_settings.m_rawResourcePath, compileDep ) );

            ResourceTypeID const extension( compileDep.GetExtension() );
            if ( IsCompileableResourceType( extension ) && !IsResourceUpToDate( ResourceID( compileDep ) ) )
            {
                return false;
            }
        }

        // Check source file for changes
        //-------------------------------------------------------------------------

        // Check against previous compilation result
        auto existingRecord = m_compiledResourceDatabase.GetRecord( resourceID );
        if ( existingRecord.IsValid() )
        {
            if ( compilerVersion != existingRecord.m_compilerVersion )
            {
                return false;
            }

            if ( fileTimestamp != existingRecord.m_fileTimestamp )
            {
                return false;
            }

            if ( sourceTimestampHash != existingRecord.m_sourceTimestampHash )
            {
                return false;
            }
        }
        else
        {
            return false;
        }

        return true;
    }

    void ResourceServer::WriteCompiledResourceRecord( CompilationRequest* pRequest )
    {
        CompiledResourceRecord record;
        record.m_resourceID = pRequest->m_resourceID;
        record.m_compilerVersion = pRequest->m_compilerVersion;
        record.m_fileTimestamp = pRequest->m_fileTimestamp;
        record.m_sourceTimestampHash = pRequest->m_sourceTimestampHash;
        m_compiledResourceDatabase.WriteRecord( record );
    }

    bool ResourceServer::IsCompileableResourceType( ResourceTypeID ID ) const
    {
        if ( !ID.IsValid() )
        {
            return false;
        }

        if ( m_pCompilerRegistry->IsVirtualResourceType( ID ) )
        {
            return false;
        }

        return m_pCompilerRegistry->HasCompilerForResourceType( ID );
    }

    //-------------------------------------------------------------------------

    void ResourceServer::RefreshAvailableMapList()
    {
        m_allMaps.clear();

        TVector<FileSystem::Path> results;
        if ( FileSystem::GetDirectoryContents( m_settings.m_rawResourcePath, results, FileSystem::DirectoryReaderOutput::OnlyFiles, FileSystem::DirectoryReaderMode::Expand, { ".map" } ) )
        {
            for ( auto const& foundMapPath : results )
            {
                m_allMaps.emplace_back( ResourceID::FromFileSystemPath( m_settings.m_rawResourcePath, foundMapPath ) );
            }
        }
    }

    void ResourceServer::StartPackaging()
    {
        EE_ASSERT( !m_isPackaging && m_resourcesToBePackaged.empty() );
        m_completedPackagingRequests.clear();

        // Package Module Resources
        //-------------------------------------------------------------------------
        // TODO: is there a less error prone mechanism for this?

        EngineModule::GetListOfAllRequiredModuleResources( m_resourcesToBePackaged );
        GameModule::GetListOfAllRequiredModuleResources( m_resourcesToBePackaged );

        // Package Selected Maps
        //-------------------------------------------------------------------------

        for ( auto const& mapID : m_mapsToBePackaged )
        {
            EnqueueResourceForPackaging( mapID );
        }

        for ( auto const& resourceID : m_resourcesToBePackaged )
        {
            CreateResourceRequest( resourceID, 0, CompilationRequest::Origin::Package );
        }

        m_isPackaging = true;
    }

    bool ResourceServer::CanStartPackaging() const
    {
        return !m_mapsToBePackaged.empty();
    }

    void ResourceServer::AddMapToPackagingList( ResourceID mapResourceID )
    {
        EE_ASSERT( mapResourceID.GetResourceTypeID() == EntityModel::SerializedEntityMap::GetStaticResourceTypeID() );
        VectorEmplaceBackUnique( m_mapsToBePackaged, mapResourceID );
    }

    void ResourceServer::RemoveMapFromPackagingList( ResourceID mapResourceID )
    {
        EE_ASSERT( mapResourceID.GetResourceTypeID() == EntityModel::SerializedEntityMap::GetStaticResourceTypeID() );
        m_mapsToBePackaged.erase_first_unsorted( mapResourceID );
    }

    void ResourceServer::EnqueueResourceForPackaging( ResourceID const& resourceID )
    {
        auto pCompiler = m_pCompilerRegistry->GetCompilerForResourceType( resourceID.GetResourceTypeID() );
        if ( pCompiler != nullptr )
        {
            // Add resource for packaging
            VectorEmplaceBackUnique( m_resourcesToBePackaged, resourceID );

            // Get all referenced resources
            TVector<ResourceID> referencedResources;
            pCompiler->GetReferencedResources( resourceID, referencedResources );

            // Recursively enqueue all referenced resources
            for ( auto const& referenceResourceID : referencedResources )
            {
                EnqueueResourceForPackaging( referenceResourceID );
            }
        }
    }
}