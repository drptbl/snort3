//--------------------------------------------------------------------------
// Copyright (C) 2014-2015 Cisco and/or its affiliates. All rights reserved.
// Copyright (C) 2005-2013 Sourcefire, Inc.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License Version 2 as published
// by the Free Software Foundation.  You may not use, modify or distribute
// this program under any other version of the GNU General Public License.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//--------------------------------------------------------------------------

// hi_util_kmap.cc author Marc Norton
// a generic map library - maps key + data pairs

#include "hi_util_kmap.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string>
#include <limits>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "hi_util_xmalloc.h"

//#define MEMASSERT(p) if(!p){printf("KMAP-No Memory: File: %s
// Line:%d\n",__FILE__,__LINE__);exit(0);}
#define MEMASSERT(p)

/*
*
*/
static void* s_malloc(int n)
{
    void* p;

    p = xmalloc(n);

    MEMASSERT(p);

    return p;
}

/*
*
*/
static void s_free(void* p)
{
    if ( p )
        xfree(p);
}

/*
*
*/
KMAP* KMapNew(KMapUserFreeFunc userfree)
{
    KMAP* km = (KMAP*)s_malloc(sizeof(KMAP) );

    if ( !km )
        return 0;

    memset(km, 0, sizeof(KMAP));

    km->userfree = userfree;

    return km;
}

/*
*
*/
void KMapSetNoCase(KMAP* km, int flag)
{
    km->nocase = flag;
}

/*
*   Free the list of key+data pair nodes - used by findfirst/next
*/
static int KMapFreeNodeList(KMAP* km)
{
    KEYNODE* k, * kold;

    for ( k=km->keylist; k; )
    {
        if ( k->key )
        {
            s_free(k->key);
        }
        if ( km->userfree && k->userdata )
        {
            km->userfree(k->userdata);
        }
        kold = k;
        k = k->next;
        s_free(kold);
    }

    return 0;
}

/*
*     Recursively walk and free nodes
*/
static void KMapFreeNode(KMAP* km, KMAPNODE* r)
{
    if ( r->sibling )
    {
        KMapFreeNode(km, r->sibling);
    }

    if ( r->child )
    {
        KMapFreeNode(km, r->child);
    }

    s_free(r);
}

/*
*  Free the KMAP and all of it's memory and nodes
*/
void KMapDelete(KMAP* km)
{
    KMAPNODE* r;
    int i;

    /* Free the tree - on root node at a time */
    for (i=0; i<256; i++)
    {
        r = km->root[i];
        if ( r )
        {
            KMapFreeNode(km,r);
        }
    }

    /* Free the node list */
    KMapFreeNodeList(km);

    s_free(km);
}

/*
*  Add key + data pair to the linked list of nodes
*/
static KEYNODE* KMapAddKeyNode(KMAP* km,void* key, int n, void* userdata)
{
    KEYNODE* knode;

    if (n <= 0)
        return 0;

    knode = (KEYNODE*)s_malloc(sizeof(KEYNODE) );
    if (!knode)
        return 0;

    memset(knode, 0, sizeof(KEYNODE) );

    knode->key = (unsigned char*)s_malloc(n); // Alloc the key space
    if ( !knode->key )
    {
        free(knode);
        return 0;
    }

    memcpy(knode->key,key,n); // Copy the key
    knode->nkey     = n;
    knode->userdata = userdata;

    if ( km->keylist ) // Insert at front of list
    {
        knode->next = km->keylist;
        km->keylist = knode;
    }
    else
    {
        km->keylist = knode;
    }

    return knode;
}

/*
*  Create a character node
*/
static KMAPNODE* KMapCreateNode(KMAP* km)
{
    KMAPNODE* mn=(KMAPNODE*)s_malloc(sizeof(KMAPNODE) );

    if (!mn)
        return NULL;

    memset(mn,0,sizeof(KMAPNODE));

    km->nchars++;

    return mn;
}

/*
*    key : ptr to bytes of data used to identify this item
*          may be text string, or binary character sequence.
*    n   : > 0 number of bytes in the key
*          <=0 key is a null terminated  ascii string
*    userdata - ptr to data to associate with this key
*
*   returns:
*            -1 - no memory
*             1 - key already in table
*             0 - key added successfully
*/
int KMapAdd(KMAP* km, void* key, int n, void* userdata)
{
    int i,ksize;
    int type = 0;
    const unsigned char* P = (unsigned char*)key;
    KMAPNODE* root;
    std::string xkey;

    if ( n <= 0 )
    {
        std::size_t tmp_len = strlen( (char*)key);
        if (tmp_len > std::numeric_limits<int>::max())
            return -99;

        n = tmp_len;
    }

    if ( km->nocase )
    {
        xkey.resize(n);

        for (i=0; i<n; i++)
            xkey[i] = std::tolower(P[i]);

        P = (const unsigned char*)xkey.c_str();
    }

    /* Save key size */
    ksize = n;

    //printf("adding key='%.*s'\n",n,P);

    /* Make sure we at least have a root character for the tree */
    if ( !km->root[ *P ] )
    {
        root = KMapCreateNode(km);
        if ( !root )
            return -1;
        km->root[ *P ] = root;
        root->nodechar = *P;
    }
    else
    {
        root = km->root[ *P ];
    }

    /* Walk exisitng Patterns */
    while ( n )
    {
        if ( root->nodechar == *P )
        {
            //printf("matched char = %c, nleft = %d\n",*P,n-1);
            P++;
            n--;
            if ( n && root->child )
            {
                root=root->child;
            }
            else /* cannot continue */
            {
                type = 0; /* Expand the tree via the child */
                break;
            }
        }
        else
        {
            if ( root->sibling )
            {
                root=root->sibling;
            }
            else /* cannot continue */
            {
                type = 1; /* Expand the tree via the sibling */
                break;
            }
        }
    }

    /*
    * Add the next char of the Keyword, if any
    */
    if ( n )
    {
        if ( type == 0 )
        {
            /*
            *  Start with a new child to finish this Keyword
                */
            //printf("added child branch nodechar = %c \n",*P);
            root->child= KMapCreateNode(km);
            if ( !root->child )
                return -1;
            root=root->child;
            root->nodechar  = *P;
            P++;
            n--;
        }
        else
        {
            /*
            *  Start a new sibling bracnch to finish this Keyword
                */
            //printf("added sibling branch nodechar = %c \n",*P);
            root->sibling= KMapCreateNode(km);
            if ( !root->sibling )
                return -1;
            root=root->sibling;
            root->nodechar  = *P;
            P++;
            n--;
        }
    }

    /*
    *    Finish the keyword as child nodes
    */
    while ( n )
    {
        //printf("added child nodechar = %c \n",*P);
        root->child = KMapCreateNode(km);
        if ( !root->child )
            return -1;
        root=root->child;
        root->nodechar  = *P;
        P++;
        n--;
    }

    /*
    * Iteration support - Add this key/data to the linked list
    * This allows us to do a findfirst/findnext search of
    * all map nodes.
    */
    if ( root->knode ) /* Already present */
        return 1;

    root->knode = KMapAddKeyNode(km, key, ksize, userdata);
    if ( !root->knode )
        return -1;

    return 0;
}

/*
*  Exact Keyword Match - unique keys, with just one piece of
*  'userdata' , for multiple entries, we could use a list
*  of 'userdata' nodes.
*/
void* KMapFind(KMAP* ks, void* key, int n)
{
    const unsigned char* T = (unsigned char*)key;
    KMAPNODE* root;
    std::string xkey;
    int i;

    if ( n <= 0 )
    {
        std::size_t tmp_len = strlen( (char*)key);
        if (tmp_len > std::numeric_limits<int>::max())
            return nullptr;

        n = tmp_len;
    }

    if ( ks->nocase )
    {
        xkey.resize(n);
        for (i=0; i<n; i++)
            xkey[i] = std::tolower(T[i]);

        T = (const unsigned char*)(xkey.c_str());
    }
    //printf("finding key='%.*s'\n",n,T);

    /* Check if any keywords start with this character */
    root = ks->root[ *T ];
    if ( !root )
        return NULL;

    while ( n )
    {
        if ( root->nodechar == *T )
        {
            T++;
            n--;
            if ( n && root->child )
            {
                root = root->child;
            }
            else /* cannot continue -- match is over */
            {
                break;
            }
        }
        else
        {
            if ( root->sibling )
            {
                root = root->sibling;
            }
            else /* cannot continue */
            {
                break;
            }
        }
    }

    if ( !n )
    {
        if (root && root->knode)
            return root->knode->userdata; /* success */
    }

    return NULL;
}

/*
*
*/
KEYNODE* KMapFindFirstKey(KMAP* km)
{
    km->keynext = km->keylist;

    if (!km->keynext)
    {
        return NULL;
    }

    return km->keynext;
}

/*
*
*/
void* KMapFindFirst(KMAP* km)
{
    km->keynext = km->keylist;

    if (!km->keynext)
    {
        return NULL;
    }

    return km->keynext->userdata;
}

/*
*
*/
KEYNODE* KMapFindNextKey(KMAP* km)
{
    if ( !km->keynext )
        return 0;

    km->keynext = km->keynext->next;

    if ( !km->keynext )
        return 0;

    return km->keynext;
}

/*
*
*/
void* KMapFindNext(KMAP* km)
{
    if ( !km->keynext )
        return 0;

    km->keynext = km->keynext->next;

    if ( !km->keynext )
        return 0;

    return km->keynext->userdata;
}

#ifdef KMAP_MAIN
/*
*
*/
int main(int argc, char** argv)
{
    int i,n=10;
    KMAP* km;
    char* p;
    char str[80];

    str[79] = '\0';

    printf("usage: kmap nkeys (default=10)\n\n");

    km = KMapNew(free);    /* use 'free' to free 'userdata' */

    KMapSetNoCase(km,1);  //need to add xlat....

    if ( argc > 1 )
    {
        n = atoi(argv[1]);
    }

    for (i=1; i<=n; i++)
    {
        snprintf(str, sizeof(str) - 1, "KeyWord%d",i);
        KMapAdd(km, str, 0 /* strlen(str) */, strupr(strdup(str)) );
        printf("Adding Key=%s\n",str);
    }
    printf("xmem: %u bytes, %d chars\n",xmalloc_bytes(),km->nchars);

    printf("\nKey Find test...\n");
    for (i=1; i<=n; i++)
    {
        snprintf(str, sizeof(str) - 1, "KeyWord%d",i);
        p = (char*)KMapFind(km, str,  0 /*strlen(str) */);
        if (p)
            printf("key=%s, data=%*s\n",str,strlen(str),p);
        else
            printf("'%s' NOT found.\n",str);
    }

    KMapSetNoCase(km,0);  // this should fail all key searches
    printf("\nKey Find test2...\n");
    for (i=1; i<=n; i++)
    {
        snprintf(str, sizeof(str) - 1, "KeyWord%d",i);
        p = (char*)KMapFind(km, str,  0 /*strlen(str) */);
        if (p)
            printf("key=%s, data=%*s\n",str,strlen(str),p);
        else
            printf("'%s' NOT found.\n",str);
    }

    printf("\nKey FindFirst/Next test...\n");
    for (p = (char*)KMapFindFirst(km); p; p=(char*)KMapFindNext(km) )
        printf("data=%s\n",p);

    printf("\nKey FindFirst/Next test done.\n");

    KMapDelete(km);

    printf("xmem: %u bytes\n",xmalloc_bytes());

    printf("normal pgm finish.\n");

    return 0;
}

#endif

