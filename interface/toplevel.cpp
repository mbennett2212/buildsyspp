/******************************************************************************
 Copyright 2013 Allied Telesis Labs Ltd. All rights reserved.
 
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

#include <buildsys.h>
#include <sys/stat.h>
#include "luainterface.h"

static int li_name(lua_State * L)
{
	if(lua_gettop(L) < 0 || lua_gettop(L) > 1) {
		throw CustomException("name() takes 1 or no argument(s)");
	}
	int args = lua_gettop(L);

	Package *P = li_get_package(L);

	if(args == 0) {
		std::string value = P->getNS()->getName();
		lua_pushstring(L, value.c_str());
		return 1;
	}
	if(lua_type(L, 1) != LUA_TSTRING)
		throw CustomException("Argument to name() must be a string");
	std::string value(lua_tostring(L, 1));

	P->setNS(P->getWorld()->findNameSpace(value));

	return 0;
}

static int li_feature(lua_State * L)
{
	if(lua_gettop(L) < 1 || lua_gettop(L) > 3) {
		throw CustomException("feature() takes 1 to 3 arguments");
	}
	Package *P = li_get_package(L);

	if(lua_gettop(L) == 1) {
		if(lua_type(L, 1) != LUA_TSTRING)
			throw CustomException("Argument to feature() must be a string");
		std::string key(lua_tostring(L, 1));
		try {
			std::string value = P->getFeature(key);
			lua_pushstring(L, value.c_str());
			P->buildDescription()->add(new
						   FeatureValueUnit(P->getWorld(), key,
								    value));
		}
		catch(NoKeyException & E) {
			lua_pushnil(L);
			P->buildDescription()->add(new FeatureNilUnit(key));
		}
		return 1;
	}
	if(lua_type(L, 1) != LUA_TSTRING)
		throw CustomException("First argument to feature() must be a string");
	if(lua_type(L, 2) != LUA_TSTRING)
		throw CustomException("Second argument to feature() must be a string");
	if(lua_gettop(L) == 3 && lua_type(L, 3) != LUA_TBOOLEAN)
		throw
		    CustomException
		    ("Third argument to feature() must be boolean, if present");
	std::string key(lua_tostring(L, 1));
	std::string value(lua_tostring(L, 2));

	if(lua_gettop(L) == 3) {
		P->getWorld()->setFeature(key, value, lua_toboolean(L, -3));
	}

	P->getWorld()->setFeature(key, value);
	return 0;
}


int li_builddir(lua_State * L)
{
	int args = lua_gettop(L);
	if(args > 1) {
		throw CustomException("builddir() takes 1 or no arguments");
	}

	Package *P = li_get_package(L);

	// create a table, since this is actually an object
	CREATE_TABLE(L, P->builddir());
	P->builddir()->lua_table(L);

	bool clean_requested = (args == 1 && lua_toboolean(L, 1));

	if((clean_requested || P->getWorld()->areCleaning()) &&
	   !P->getWorld()->areParseOnly() &&
	   !(P->getWorld()->forcedMode() && !P->getWorld()->isForced(P->getName()))) {
		log(P, "Cleaning");
		P->builddir()->clean();
	}

	return 1;
}

int li_intercept(lua_State * L)
{
	Package *P = li_get_package(L);

	P->setIntercept();

	return 0;
}

int li_keepstaging(lua_State * L)
{
	Package *P = li_get_package(L);

	P->setSuppressRemoveStaging();

	return 0;
}

static void depend(Package * P, NameSpace * ns, bool locally, const std::string & name)
{
	Package *p = NULL;
	// create the Package
	if(ns) {
		p = ns->findPackage(name);
	} else {
		p = P->getNS()->findPackage(name);
	}
	if(p == NULL) {
		throw CustomException("Failed to create or find Package");
	}

	P->depend(new PackageDepend(p, locally));
}

int li_depend(lua_State * L)
{
	if(!(lua_gettop(L) == 1 || lua_gettop(L) == 2)) {
		throw CustomException("depend() takes 1 argument or 2 arguments");
	}

	NameSpace *ns = NULL;
	// get the current package
	Package *P = li_get_package(L);

	if(lua_type(L, 1) == LUA_TSTRING) {
		if(lua_gettop(L) == 2) {
			if(lua_type(L, 2) != LUA_TSTRING) {
				throw
				    CustomException
				    ("depend() takes a string as the second argument");
			}
			ns = P->getWorld()->findNameSpace(std::string(lua_tostring(L, 2)));
		}
		depend(P, ns, false, std::string(lua_tostring(L, 1)));
	} else if(lua_istable(L, 1)) {
		std::list < std::string > package_names;
		bool locally = false;
		lua_pushnil(L);	/* first key */
		while(lua_next(L, 1) != 0) {
			/* uses 'key' (at index -2) and 'value' (at index -1) */
			if(lua_type(L, -2) != LUA_TSTRING) {
				throw
				    CustomException
				    ("depend() requires a table with strings as keys\n");
			}
			std::string key(lua_tostring(L, -2));
			if(key == "package" || key == "packages") {
				if(lua_type(L, -1) == LUA_TSTRING) {
					package_names.push_back(std::string(lua_tostring
									    (L, -1)));
				} else if(lua_istable(L, -1)) {
					lua_pushnil(L);
					while(lua_next(L, -2) != 0) {
						if(lua_type(L, -1) != LUA_TSTRING) {
							throw
							    CustomException
							    ("depend() requires a single package name or table of package names\n");
						}
						package_names.push_back(std::string
									(lua_tostring
									 (L, -1)));
						lua_pop(L, 1);
					}
				} else {
					throw
					    CustomException
					    ("depend() requires a single package name or table of package names\n");
				}
			} else if(key == "namespace") {
				if(lua_type(L, -1) != LUA_TSTRING) {
					throw
					    CustomException
					    ("depend() requires a string for the namespace name\n");
				}
				ns = P->
				    getWorld()->findNameSpace(std::
							      string(lua_tostring(L, -1)));
			} else if(key == "locally") {
				if(lua_type(L, -1) == LUA_TBOOLEAN) {
					locally = lua_toboolean(L, -1);
				} else if(lua_type(L, -1) == LUA_TSTRING) {
					std::string value(lua_tostring(L, -1));
					if(value == "true") {
						locally = true;
					}
				}
			}
			/* removes 'value'; keeps 'key' for next iteration */
			lua_pop(L, 1);
		}
		for(auto iter = package_names.begin(); iter != package_names.end(); iter++) {
			depend(P, ns, locally, (*iter));
		}
	} else {
		throw CustomException("depend() takes a string or a table of strings");
	}

	return 0;
}

int li_buildlocally(lua_State * L)
{
	if(lua_gettop(L) != 0) {
		throw CustomException("buildlocally() takes no arguments");
	}

	Package *P = li_get_package(L);

	// Do not try and download the final result for this package
	// probably because it breaks something else that builds later
	P->disableFetchFrom();

	return 0;
}


int li_hashoutput(lua_State * L)
{
	if(lua_gettop(L) != 0) {
		throw CustomException("buildlocally() takes no arguments");
	}

	Package *P = li_get_package(L);

	// Instead of depender using build.info hash
	// create an output.info and get them to hash that
	// useful for kernel-headers and other packages
	// that produce data that changes less often
	// than the sources
	P->setHashOutput();

	return 0;
}

int li_require(lua_State * L)
{
	if(lua_gettop(L) != 1) {
		throw CustomException("require() takes 1 argument");
	}

	if(!lua_isstring(L, 1)) {
		throw CustomException("Argument to require() must be a string");
	}

	Package *P = li_get_package(L);

	std::string fname = string_format("%s.lua", lua_tostring(L, 1));
	std::string relative_fname = P->relative_fetch_path(fname, true);

	// Check whether the relative file path exists. If it does
	// not exist then we use the original file path
	if(!filesystem::exists(relative_fname)) {
		throw FileNotFoundException("require", fname);
	}

	P->getLua()->processFile(relative_fname);
	P->buildDescription()->add(new RequireFileUnit(relative_fname, fname));

	return 0;
}

bool buildsys::interfaceSetup(Lua * lua)
{
	lua->registerFunc("builddir", li_builddir);
	lua->registerFunc("depend", li_depend);
	lua->registerFunc("feature", li_feature);
	lua->registerFunc("intercept", li_intercept);
	lua->registerFunc("keepstaging", li_keepstaging);
	lua->registerFunc("name", li_name);
	lua->registerFunc("buildlocally", li_buildlocally);
	lua->registerFunc("hashoutput", li_hashoutput);
	lua->registerFunc("require", li_require);

	return true;
}
