/*
 * cppcheck - c/c++ syntax checking
 * Copyright (C) 2007-2008 Daniel Marjamäki, Reijo Tomperi, Nicolas Le Cam
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/
 */

//---------------------------------------------------------------------------
#include "checkclass.h"

#include <locale>

#include <string>
#include <sstream>
#include <cstring>
#include <algorithm>


#ifdef __BORLANDC__
#include <ctype.h>
#include <mem.h>
#endif
//---------------------------------------------------------------------------

CheckClass::CheckClass( const Tokenizer *tokenizer, const Settings &settings, ErrorLogger *errorLogger )
{
    _tokenizer = tokenizer;
    _settings = settings;
    _errorLogger = errorLogger;
}

CheckClass::~CheckClass()
{

}

//---------------------------------------------------------------------------

struct CheckClass::VAR *CheckClass::ClassChecking_GetVarList(const TOKEN *tok1)
{
    // Get variable list..
    struct VAR *varlist = NULL;
    unsigned int indentlevel = 0;
    for (const TOKEN *tok = tok1; tok; tok = tok->next())
    {
        if (!tok->next())
            break;

        if (tok->str() == "{")
            ++indentlevel;
        else if (tok->str() == "}")
        {
            if (indentlevel <= 1)
                break;
            --indentlevel;
        }

        if ( indentlevel != 1 )
            continue;

        // "private:" "public:" "protected:" etc
        bool b = bool((*tok->strAt(0) != ':') && strchr(tok->strAt(0), ':') != 0);

        // Search for start of statement..
        if ( ! TOKEN::Match(tok, "[;{}]") && ! b )
            continue;

        // This is the start of a statement
        const TOKEN *next = tok->next();
        const char *varname = 0;

        // Is it a variable declaration?
        if ( TOKEN::Match(next,"%type% %var% ;") )
        {
            if ( next->isStandardType() )
                varname = next->strAt(1);
        }

        // Pointer?
        else if ( TOKEN::Match(next, "%type% * %var% ;") )
        {
            varname = next->strAt(2);
        }

        // If the varname was set in one of the two if-block above, create a entry for this variable..
        if (varname)
        {
            struct VAR *var = new VAR;
            memset(var, 0, sizeof(struct VAR));
            var->name = varname;
            var->init = false;
            var->next = varlist;
            varlist   = var;
        }
    }

    return varlist;
}
//---------------------------------------------------------------------------

const TOKEN * CheckClass::FindClassFunction( const TOKEN *tok, const char classname[], const char funcname[], int &indentlevel )
{
    if ( indentlevel < 0 || tok == NULL )
        return NULL;

    std::ostringstream classPattern;
    classPattern << "class " << classname << " :|{";

    std::ostringstream internalPattern;
    internalPattern << funcname << " (";

    std::ostringstream externalPattern;
    externalPattern << classname << " :: " << funcname << " (";

    for ( ;tok; tok = tok->next() )
    {
        if ( indentlevel == 0 && TOKEN::Match(tok, classPattern.str().c_str()) )
        {
            while ( tok && tok->str() != "{" )
                tok = tok->next();
            if ( tok )
                tok = tok->next();
            if ( ! tok )
                break;
            indentlevel = 1;
        }

        if ( tok->str() == "{" )
        {
            // If indentlevel==0 don't go to indentlevel 1. Skip the block.
            if ( indentlevel > 0 )
                ++indentlevel;

            else
            {
                for ( ; tok; tok = tok->next() )
                {
                    if ( tok->str() == "{" )
                        ++indentlevel;
                    else if ( tok->str() == "}" )
                    {
                        --indentlevel;
                        if ( indentlevel <= 0 )
                            break;
                    }
                }
                if ( tok == NULL )
                    return NULL;

                continue;
            }
        }

        if ( tok->str() == "}" )
        {
            indentlevel--;
            if ( indentlevel < 0 )
                return NULL;
        }

        if ( indentlevel == 1 )
        {
            // Member function implemented in the class declaration?
            if (!TOKEN::Match(tok,"~") && TOKEN::Match(tok->next(), internalPattern.str().c_str()))
            {
                const TOKEN *tok2 = tok;
                while ( tok2 && tok2->str() != "{" && tok2->str() != ";" )
                    tok2 = tok2->next();
                if ( tok2 && tok2->str() == "{" )
                    return tok->next();
            }
        }

        else if ( indentlevel == 0 && TOKEN::Match(tok, externalPattern.str().c_str()) )
        {
            return tok;
        }
    }

    // Not found
    return NULL;
}
//---------------------------------------------------------------------------

void CheckClass::InitVar(struct VAR *varlist, const char varname[])
{
    for (struct VAR *var = varlist; var; var = var->next)
    {
        if ( strcmp(var->name, varname) == 0 )
        {
            var->init = true;
            break;
        }
    }
}
//---------------------------------------------------------------------------

void CheckClass::ClassChecking_VarList_Initialize(const TOKEN *tok1, const TOKEN *ftok, struct VAR *varlist, const char classname[], std::list<std::string> &callstack)
{
    bool Assign = false;
    unsigned int indentlevel = 0;

    for (; ftok; ftok = ftok->next())
    {
        if (!ftok->next())
            break;

        // Class constructor.. initializing variables like this
        // clKalle::clKalle() : var(value) { }
        if (indentlevel==0)
        {
            if (Assign && TOKEN::Match(ftok, "%var% ("))
            {
                InitVar( varlist, ftok->aaaa() );
            }

            Assign |= (ftok->str() == ":");
        }


        if (ftok->str() == "{")
        {
            indentlevel++;
            Assign = false;
        }

        if (ftok->str() == "}")
        {
            if (indentlevel <= 1)
                break;
            indentlevel--;
        }

        if ( indentlevel < 1 )
            continue;

        // Before a new statement there is "[{};)=]" or "else"
        if ( ! TOKEN::Match(ftok, "[{};)=]") && ! TOKEN::Match(ftok, "else") )
            continue;

        // Using the operator= function to initialize all variables..
        if ( TOKEN::Match(ftok->next(), "* this = ") )
        {
            for (struct VAR *var = varlist; var; var = var->next)
                var->init = true;
            break;
        }

        if (!TOKEN::Match(ftok->next(), "%var%") && !TOKEN::Match(ftok->next(), "this . %var%"))
            continue;

        // Goto the first token in this statement..
        ftok = ftok->next();

        // Skip "this->"
        if ( TOKEN::Match(ftok, "this .") )
            ftok = ftok->tokAt(2);

        // Clearing all variables..
        if (TOKEN::Match(ftok,"memset ( this ,"))
        {
            for (struct VAR *var = varlist; var; var = var->next)
                var->init = true;
            break;
        }

        // Calling member function?
        else if (TOKEN::Match(ftok, "%var% ("))
        {
            // No recursive calls!
            if ( std::find(callstack.begin(),callstack.end(),ftok->str()) == callstack.end() )
            {
                callstack.push_back( ftok->str() );
                int i = 0;
                const TOKEN *ftok2 = FindClassFunction( tok1, classname, ftok->aaaa(), i );
                ClassChecking_VarList_Initialize(tok1, ftok2, varlist, classname, callstack);
            }
        }

        // Assignment of member variable?
        else if (TOKEN::Match(ftok, "%var% ="))
        {
            InitVar( varlist, ftok->aaaa() );
        }

        // The functions 'clear' and 'Clear' are supposed to initialize variable.
        if (TOKEN::Match(ftok,"%var% . clear (") || TOKEN::Match(ftok,"%var% . Clear ("))
        {
            InitVar( varlist, ftok->aaaa() );
        }
    }
}






//---------------------------------------------------------------------------
// ClassCheck: Check that all class constructors are ok.
//---------------------------------------------------------------------------

void CheckClass::constructors()
{
    const char pattern_class[] = "class %var% {";

    // Locate class
    const TOKEN *tok1 = TOKEN::findmatch( _tokenizer->tokens(), pattern_class );
    while (tok1)
    {
        const char *className[2];
        className[0] = tok1->strAt( 1 );
        className[1] = 0;

        // TODO: handling of private constructors should be improved.
        bool hasPrivateConstructor = false;
        {
            int indentlevel = 0;
            bool isPrivate = true;
            for ( const TOKEN *tok = tok1; tok; tok = tok->next() )
            {
                // Indentation
                if ( tok->str() == "{" )
                    ++indentlevel;

                else if ( tok->str() == "}" )
                {
                    --indentlevel;
                    if (indentlevel <= 0)
                        break;
                }

                // Parse class contents (indentlevel == 1)..
                if ( indentlevel == 1 )
                {
                    // What section are we in.. private/non-private
                    if ( tok->str() == "private:" )
                        isPrivate = true;
                    else if ( tok->str() == "protected:" || tok->str() == "public:" )
                        isPrivate = false;

                    // Is there a private constructor?
                    else if ( isPrivate && TOKEN::Match(tok, "%var1% (", className) )
                    {
                        hasPrivateConstructor = true;
                        break;
                    }
                }
            }
        }

        if ( hasPrivateConstructor )
        {
            // TODO: Handle private constructors.
            // Right now to avoid false positives I just bail out
            tok1 = TOKEN::findmatch( tok1->next(), pattern_class );
            continue;
        }

        // Are there a class constructor?
        const TOKEN *constructor_token = TOKEN::findmatch( tok1, "%any% %var1% (", className );
        while ( TOKEN::Match( constructor_token, "~" ) )
            constructor_token = TOKEN::findmatch( constructor_token->next(), "%any% %var1% (", className );

        // There are no constructor.
        if ( ! constructor_token )
        {
            // If "--style" has been given, give a warning
            if ( _settings._checkCodingStyle )
            {
                // If the class has member variables there should be an constructor
                struct VAR *varlist = ClassChecking_GetVarList(tok1);
                if ( varlist )
                {
                    std::ostringstream ostr;
                    ostr << _tokenizer->fileLine(tok1);
                    ostr << " The class '" << className[0] << "' has no constructor";
                    _errorLogger->reportErr(ostr.str());
                }
                // Delete the varlist..
                while (varlist)
                {
                    struct VAR *nextvar = varlist->next;
                    delete varlist;
                    varlist = nextvar;
                }
            }

            tok1 = TOKEN::findmatch( tok1->next(), pattern_class );
            continue;
        }

        // Check that all member variables are initialized..
        struct VAR *varlist = ClassChecking_GetVarList(tok1);

        // Check constructors
        CheckConstructors( tok1, varlist, className[0] );

        // Check assignment operators
        CheckConstructors( tok1, varlist, "operator =" );

        // Delete the varlist..
        while (varlist)
        {
            struct VAR *nextvar = varlist->next;
            delete varlist;
            varlist = nextvar;
        }

        tok1 = TOKEN::findmatch( tok1->next(), pattern_class );
    }
}

void CheckClass::CheckConstructors(const TOKEN *tok1, struct VAR *varlist, const char funcname[])
{
    const char * const className = tok1->strAt(1);

    int indentlevel = 0;
    const TOKEN *constructor_token = FindClassFunction( tok1, className, funcname, indentlevel );
    std::list<std::string> callstack;
    ClassChecking_VarList_Initialize(tok1, constructor_token, varlist, className, callstack);
    while ( constructor_token )
    {
        // Check if any variables are uninitialized
        for (struct VAR *var = varlist; var; var = var->next)
        {
            if ( var->init )
                continue;

            // Is it a static member variable?
            std::ostringstream pattern;
            pattern << className << "::" << var->name << "=";
            if (TOKEN::findmatch(_tokenizer->tokens(), pattern.str().c_str()))
                continue;

            // It's non-static and it's not initialized => error
            std::ostringstream ostr;
            ostr << _tokenizer->fileLine(constructor_token);
            ostr << " Uninitialized member variable '" << className << "::" << var->name << "'";
            _errorLogger->reportErr(ostr.str());
        }

        for ( struct VAR *var = varlist; var; var = var->next )
            var->init = false;

        constructor_token = FindClassFunction( constructor_token->next(), className, funcname, indentlevel );
        callstack.clear();
        ClassChecking_VarList_Initialize(tok1, constructor_token, varlist, className, callstack);
    }
}


//---------------------------------------------------------------------------
// ClassCheck: Unused private functions
//---------------------------------------------------------------------------

void CheckClass::privateFunctions()
{
    // Locate some class
    for (const TOKEN *tok1 = TOKEN::findmatch(_tokenizer->tokens(), "class %var% {"); tok1; tok1 = TOKEN::findmatch(tok1->next(), "class %var% {"))
    {
        const std::string &classname = tok1->next()->str();

        // The class implementation must be available..
        const std::string classconstructor(classname + " :: " + classname);
        if (!TOKEN::findmatch(_tokenizer->tokens(), classconstructor.c_str()))
            continue;

        // Get private functions..
        std::list<std::string> FuncList;
        FuncList.clear();
        bool priv = false;
        unsigned int indent_level = 0;
        for (const TOKEN *tok = tok1; tok; tok = tok->next())
        {
            if (TOKEN::Match(tok,"friend %var%"))
            {
                // Todo: Handle friend classes
                FuncList.clear();
                break;
            }

            if (tok->str() == "{")
                indent_level++;
            else if (tok->str() == "}")
            {
                if (indent_level <= 1)
                    break;
                indent_level--;
            }
            else if (tok->str() == "private:")
                priv = true;
            else if (tok->str() == "public:")
                priv = false;
            else if (tok->str() == "protected:")
                priv = false;
            else if (priv && indent_level == 1)
            {
                if ( TOKEN::Match(tok, "typedef %type% (") )
                    tok = tok->tokAt(2);

                if (TOKEN::Match(tok, "%var% (") &&
                    !TOKEN::Match(tok,classname.c_str()))
                {
                    FuncList.push_back(tok->str());
                }
            }
        }

        // Check that all private functions are used..
        const std::string pattern_function(classname + " ::");
        bool HasFuncImpl = false;
        const TOKEN *ftok = _tokenizer->tokens();
        while (ftok)
        {
            ftok = TOKEN::findmatch(ftok,pattern_function.c_str());
            int numpar = 0;
            while (ftok && !TOKEN::Match(ftok, "[;{]"))
            {
                if (ftok->str() == "(")
                    ++numpar;
                else if (ftok->str() == ")")
                    --numpar;
                ftok = ftok->next();
            }

            if (!ftok)
                break;

            if (ftok->str() != ";" && numpar == 0)
            {
                HasFuncImpl = true;

                indent_level = 0;
                while (ftok)
                {
                    if (ftok->str() == "{")
                        indent_level++;
                    if (ftok->str() == "}")
                    {
                        if (indent_level<=1)
                            break;
                        indent_level--;
                    }
                    if (TOKEN::Match( ftok->next(), "(") )
                        FuncList.remove(ftok->str());
                    ftok = ftok->next();
                }
            }

            if (ftok)
                ftok = ftok->next();
        }

        while (HasFuncImpl && !FuncList.empty())
        {
            // Final check; check if the function pointer is used somewhere..
            const std::string _pattern("return|(|)|,|= " + FuncList.front());
            if (!TOKEN::findmatch(_tokenizer->tokens(), _pattern.c_str()))
            {
                std::ostringstream ostr;
                ostr << "Class '" << classname << "', unused private function: '" << FuncList.front() << "'";
                _errorLogger->reportErr(ostr.str());
            }
            FuncList.pop_front();
        }
    }
}

//---------------------------------------------------------------------------
// ClassCheck: Check that memset is not used on classes
//---------------------------------------------------------------------------

void CheckClass::noMemset()
{
    // Locate all 'memset' tokens..
    for (const TOKEN *tok = _tokenizer->tokens(); tok; tok = tok->next())
    {
        if (!TOKEN::Match(tok,"memset|memcpy|memmove"))
            continue;

        // Todo: Handle memcpy and memmove
        const char *type = NULL;
        if (TOKEN::Match(tok, "memset ( %var% , %num% , sizeof ( %type% ) )"))
            type = tok->strAt(8);
        else if (TOKEN::Match(tok, "memset ( & %var% , %num% , sizeof ( %type% ) )"))
            type = tok->strAt(9);
        else if (TOKEN::Match(tok, "memset ( %var% , %num% , sizeof ( struct %type% ) )"))
            type = tok->strAt(9);
        else if (TOKEN::Match(tok, "memset ( & %var% , %num% , sizeof ( struct %type% ) )"))
            type = tok->strAt(10);
        else if (TOKEN::Match(tok, "%type% ( %var% , %var% , sizeof ( %type% ) )"))
            type = tok->strAt(8);

        // No type defined => The tokens didn't match
        if (!(type && type[0]))
            continue;

        // Warn if type is a class..
        const std::string pattern1(std::string("class ") + type);
        if (TOKEN::findmatch(_tokenizer->tokens(),pattern1.c_str()))
        {
            std::ostringstream ostr;
            ostr << _tokenizer->fileLine(tok) << ": Using '" << tok->str() << "' on class.";
            _errorLogger->reportErr(ostr.str());
            continue;
        }

        // Warn if type is a struct that contains any std::*
        const std::string pattern2(std::string("struct ") + type);
        for (const TOKEN *tstruct = TOKEN::findmatch(_tokenizer->tokens(), pattern2.c_str()); tstruct; tstruct = tstruct->next())
        {
            if (tstruct->str() == "}")
                break;

            if (TOKEN::Match(tstruct, "std :: %type% %var% ;"))
            {
                std::ostringstream ostr;
                ostr << _tokenizer->fileLine(tok) << ": Using '" << tok->str() << "' on struct that contains a 'std::" << tstruct->strAt(2) << "'";
                _errorLogger->reportErr(ostr.str());
                break;
            }
        }
    }
}
//---------------------------------------------------------------------------




//---------------------------------------------------------------------------
// ClassCheck: "void operator=("
//---------------------------------------------------------------------------

void CheckClass::operatorEq()
{
    if (const TOKEN *tok = TOKEN::findmatch(_tokenizer->tokens(), "void operator = ("))
    {
        std::ostringstream ostr;
        ostr << _tokenizer->fileLine(tok) << ": 'operator=' should return something";
        _errorLogger->reportErr(ostr.str());
    }
}
//---------------------------------------------------------------------------




//---------------------------------------------------------------------------
// A destructor in a base class should be virtual
//---------------------------------------------------------------------------

void CheckClass::virtualDestructor()
{
    const char pattern_classdecl[] = "class %var% : %var%";

    const TOKEN *derived = _tokenizer->tokens();
    while ((derived = TOKEN::findmatch(derived, pattern_classdecl)) != NULL)
    {
        // Check that the derived class has a non empty destructor..
        {
            std::ostringstream destructorPattern;
            destructorPattern << "~ " << derived->strAt(1) << " ( ) {";
            const TOKEN *derived_destructor = TOKEN::findmatch( _tokenizer->tokens(), destructorPattern.str().c_str() );

            // No destructor..
            if ( ! derived_destructor )
            {
                derived = derived->next();
                continue;
            }

            // Empty destructor..
            if ( TOKEN::Match(derived_destructor, "~ %var% ( ) { }") )
            {
                derived = derived->next();
                continue;
            }
        }

        const TOKEN *derivedClass = derived->tokAt(1);

        // Iterate through each base class...
        derived = derived->tokAt(3);
        while ( TOKEN::Match(derived, "%var%") )
        {
            bool isPublic = TOKEN::Match(derived, "public");

            // What kind of inheritance is it.. public|protected|private
            if ( TOKEN::Match( derived, "public|protected|private" ) )
                derived = derived->next();

            // Name of base class..
            const char *baseName[2];
            baseName[0] = derived->strAt(0);
            baseName[1] = 0;

            // Update derived so it's ready for the next loop.
            derived = derived->next();
            if ( TOKEN::Match(derived, ",") )
                derived = derived->next();

            // If not public inheritance, skip checking of this base class..
            if ( ! isPublic )
                continue;

            // Find the destructor declaration for the base class.
            const TOKEN *base = TOKEN::findmatch(_tokenizer->tokens(), "%any% ~ %var1% (", baseName);
            while (TOKEN::Match(base, "::"))
                base = TOKEN::findmatch(base->next(), "%any% ~ %var1% (", baseName);

            while (TOKEN::Match(base, "%var%") && !TOKEN::Match(base, "virtual"))
                base = base->previous();

            // Check that there is a destructor..
            if ( ! base )
            {
                // Is the class declaration available?
                base = TOKEN::findmatch(_tokenizer->tokens(), "class %var1% :|{", baseName);
                if ( base )
                {
                    std::ostringstream errmsg;
                    errmsg << _tokenizer->fileLine(base) << ": Class " << baseName[0] << " which is inherited by class " << derivedClass->str() << " does not have a virtual destructor";
                    _errorLogger->reportErr(errmsg.str());
                }
            }

            // There is a destructor. Check that it's virtual..
            else if ( ! TOKEN::Match(base, "virtual") )
            {
                std::ostringstream errmsg;
                errmsg << _tokenizer->fileLine(base) << ": Class " << baseName[0] << " which is inherited by class " << derivedClass->str() << " does not have a virtual destructor";
                _errorLogger->reportErr(errmsg.str());
            }
        }
    }
}
//---------------------------------------------------------------------------

