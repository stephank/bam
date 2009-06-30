#include <stdio.h>
#include <string.h>

/* lua includes */
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "node.h"
#include "context.h"
#include "support.h"
#include "luafuncs.h"
#include "path.h"
#include "session.h"

/*
	add_job(string output, string label, string command)
*/
int lf_add_job(lua_State *L)
{
	struct NODE *node;
	struct CONTEXT *context;
	int n = lua_gettop(L);
	int i;
	if(n != 3)
	{
		lua_pushstring(L, "add_job: incorrect number of arguments");
		lua_error(L);
	}

	/* fetch contexst from lua */
	context = context_get_pointer(L);

	/* create the node */
	i = node_create(&node, context->graph, lua_tostring(L,1), lua_tostring(L,2), lua_tostring(L,3));
	if(i == NODECREATE_NOTNICE)
	{
		printf("%s: '%s' is not nice\n", session.name, lua_tostring(L,2));
		lua_pushstring(L, "add_job: path is not nice");
		lua_error(L);
	}
	else if(i == NODECREATE_EXISTS)
	{
		printf("%s: '%s' already exists\n", session.name, lua_tostring(L,2));
		lua_pushstring(L, "add_job: node already exists");
		lua_error(L);
	}
	else if(i != NODECREATE_OK)
	{
		lua_pushstring(L, "add_job: unknown error creating node");
		lua_error(L);
	}

	return 0;
}

/* ********** */
int lf_add_dependency(lua_State *L)
{
	struct NODE *node;
	struct CONTEXT *context;

	int n = lua_gettop(L);
	int i;
	
	if(n < 2)
	{
		lua_pushstring(L, "add_dep: to few arguments");
		lua_error(L);
	}

	context = context_get_pointer(L);

	node = node_find(context->graph, lua_tostring(L,1));
	if(!node)
	{
		char buf[MAX_PATH_LENGTH];
		sprintf(buf, "add_dep: couldn't find node with name '%s'", lua_tostring(L,1));
		lua_pushstring(L, buf);
		lua_error(L);
	}
	
	/* seek deps */
	for(i = 2; i <= n; ++i)
	{
		if(lua_isstring(L,n))
		{
			if(!node_add_dependency(node, lua_tostring(L,n)))
			{
				lua_pushstring(L, "add_dep: could not add dependency");
				lua_error(L);
			}
		}
		else
		{
			lua_pushstring(L, "add_dep: dependency is not a string");
			lua_error(L);
		}
	}
	
	return 0;
}

/* default_target(string filename) */
int lf_default_target(lua_State *L)
{
	struct NODE *node;
	struct CONTEXT *context;

	int n = lua_gettop(L);
	if(n != 1)
	{
		lua_pushstring(L, "default_target: incorrect number of arguments");
		lua_error(L);
	}
	
	if(!lua_isstring(L,1))
	{
		lua_pushstring(L, "default_target: expected string");
		lua_error(L);
	}

	/* fetch context from lua */
	context = context_get_pointer(L);

	/* search for the node */
	node = node_find(context->graph, lua_tostring(L,1));
	if(!node)
	{
		lua_pushstring(L, "default_target: node not found");
		lua_error(L);
	}
	
	/* set target */
	context_default_target(context, node);
	return 0;
}

/* update_globalstamp(string filename) */
int lf_update_globalstamp(lua_State *L)
{
	struct CONTEXT *context;

	int n = lua_gettop(L);
	time_t file_stamp;
	
	if(n < 1)
	{
		lua_pushstring(L, "update_globalstamp: to few arguments");
		lua_error(L);
	}

	context = context_get_pointer(L);
	file_stamp = file_timestamp(lua_tostring(L,1)); /* update global timestamp */
	
	if(file_stamp > context->globaltimestamp)
		context->globaltimestamp = file_stamp;
	
	return 0;
}


/* loadfile(filename) */
int lf_loadfile(lua_State *L)
{
	int n = lua_gettop(L);
	int ret = 0;
	
	if(n < 1)
	{
		lua_pushstring(L, "loadfile: to few arguments");
		lua_error(L);
	}

	if(session.verbose)
		printf("%s: reading script from '%s'\n", session.name, lua_tostring(L,1));
	
	ret = luaL_loadfile(L, lua_tostring(L,1));
	if(ret != 0)
		lua_error(L);
		
	return 1;
}


/* ** */
static void debug_print_lua_value(lua_State *L, int i)
{
	if(lua_type(L,i) == LUA_TNIL)
		printf("nil");
	else if(lua_type(L,i) == LUA_TSTRING)
		printf("'%s'", lua_tostring(L,i));
	else if(lua_type(L,i) == LUA_TNUMBER)
		printf("%f", lua_tonumber(L,i));
	else if(lua_type(L,i) == LUA_TBOOLEAN)
	{
		if(lua_toboolean(L,i))
			printf("true");
		else
			printf("false");
	}
	else if(lua_type(L,i) == LUA_TTABLE)
	{
		printf("{...}");
	}
	else
		printf("%p (%s (%d))", lua_topointer(L,i), lua_typename(L,lua_type(L,i)), lua_type(L,i));
}


/* error function */
int lf_errorfunc(lua_State *L)
{
	int depth = 0;
	int frameskip = 1;
	lua_Debug frame;

	if(session.report_color)
		printf("\033[01;31m%s\033[00m\n", lua_tostring(L,-1));
	else
		printf("%s\n", lua_tostring(L,-1));
	
	printf("stack traceback:\n");
	
	while(lua_getstack(L, depth, &frame) == 1)
	{
		depth++;
		
		lua_getinfo(L, "nlSf", &frame);

		/* check for functions that just report errors. these frames just confuses more then they help */
		if(frameskip && strcmp(frame.short_src, "[C]") == 0 && frame.currentline == -1)
			continue;
		frameskip = 0;
		
		/* print stack frame */
		printf("  %s(%d): %s %s\n", frame.short_src, frame.currentline, frame.name, frame.namewhat);
		
		/* print all local variables for the frame */
		{
			int i;
			const char *name = 0;
			
			i = 1;
			while((name = lua_getlocal(L, &frame, i)) != NULL)
			{
				printf("    %s = ", name);
				debug_print_lua_value(L,-1);
				printf("\n");
				lua_pop(L,1);
				i++;
			}
			
			i = 1;
			while((name = lua_getupvalue(L, -1, i)) != NULL)
			{
				printf("    upvalue #%d: %s ", i-1, name);
				debug_print_lua_value(L, -1);
				lua_pop(L,1);
				i++;
			}
		}
	}
	return 1;
}

int lf_panicfunc(lua_State *L)
{
	printf("%s: PANIC! I'm gonna segfault now\n", session.name);
	*(int*)0 = 0;
	return 0;
}

