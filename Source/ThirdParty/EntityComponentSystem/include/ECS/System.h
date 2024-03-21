/*
	Author : Tobias Stein
	Date   : 4th July, 2016
	File   : System.h

	System base class.

	All Rights Reserved. (c) Copyright 2016.
*/

#pragma once
#ifndef ECS__SYSTEM_H__
#define ECS__SYSTEM_H__

#include "API.h"

#include "ISystem.h"
#include "util/FamilyTypeID.h"


namespace ECS
{
	template<class T>
	class System : public ISystem
	{
		friend class SystemManager;

	private:

		SystemManager* m_SystemManagerInstance;

	protected:

		DECLARE_LOGGER		

	public:

		static const SystemTypeId STATIC_SYSTEM_TYPE_ID;

	protected:

		System()
		{
			DEFINE_LOGGER(typeid(T).name())
			LOG_INFO("System %s created.", typeid(T).name());
		}

	public:

		virtual ~System() override
		{
			LOG_INFO("System %s released.", typeid(T).name());
		}

		virtual inline SystemTypeId GetStaticSystemTypeID() const override
		{
			return STATIC_SYSTEM_TYPE_ID;
		}

		virtual inline const char* GetSystemTypeName() const override
		{
			static const char* SYSTEM_TYPE_NAME { typeid(T).name() };
			return SYSTEM_TYPE_NAME;
		}

		///-------------------------------------------------------------------------------------------------
		/// Fn:	template<class... Dependencies> void System::AddDependencies(Dependencies&&... dependencies)
		///
		/// Summary:	Adds a new dependencies for this system.
		///
		/// Author:	Tobias Stein
		///
		/// Date:	28/09/2017
		///
		/// Typeparams:
		/// Dependencies - 	Type of the dependencies.
		/// Parameters:
		/// dependencies - 	Variable arguments providing [in,out] The dependencies.
		///-------------------------------------------------------------------------------------------------

		template<class... Dependencies>
		void AddDependencies(Dependencies&&... dependencies)
		{
			this->m_SystemManagerInstance->AddSystemDependency(this, std::forward<Dependencies>(dependencies)...);
		}

		virtual void PreUpdate( [[maybe_unused]] f32 dt ) override
		{}

		virtual void Update( [[maybe_unused]] f32 dt ) override
		{}

		virtual void PostUpdate( [[maybe_unused]]f32 dt ) override
		{}

		virtual void OnFrameStart() override
		{}

		virtual void OnFrameEnd() override
		{}

	}; // class System<T>

	template<class T>
	const SystemTypeId System<T>::STATIC_SYSTEM_TYPE_ID = util::Internal::FamilyTypeID<ISystem>::Get<T>();

} // namespace ECS

#endif // ECS__SYSTEM_H__