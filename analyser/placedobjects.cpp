#include <list>
#include <map>
using namespace std;

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <math.h>

#include "tinyxml.h"

#include "outstreams.hpp"
#include "config.hpp"
#include "coordinates.hpp"
#include "SC2Map.hpp"
#include "sc2mapTypes.hpp"






struct FootToApply
{
  point  loc;
  float  rot;
  //string type;
  int type;

  string name;
};


static list<point>       pathingFills;
static list<FootToApply> footsToApply;



void SC2Map::processPlacedObject( ObjectMode objMode, TiXmlElement* object )
{
  char* objPoint;
  char* objUnit;
  char* objDoodad;
  char* objGroup;
  char* attName;
  char* attType;
  char* attPosition;
  char* attRotation;
  char* attUnitType;
  char* attResources;
  char* chdFlag;
  char* flgIndex;
  char* flgValue;

  char* poWatchtower;
  char* poRichMineralField;

  list<string> objsToIgnore;


  switch( objMode )
  {
    case OBJMODE_BETAPH1_v26:
      objPoint     = (char*)"Point";
      objUnit      = (char*)"Unit";
      objDoodad    = (char*)"Doodad";
      objGroup     = (char*)"Group";
      attName      = (char*)"name";
      attType      = (char*)"type";
      attPosition  = (char*)"position";
      attRotation  = (char*)"rotation";
      attUnitType  = (char*)"unitType";
      attResources = (char*)"resources";
      chdFlag      = (char*)"";
      flgIndex     = (char*)"";
      flgValue     = (char*)"";
      poWatchtower = (char*)"Observatory";
      poRichMineralField = (char*)"HighYieldMineralField";
    break;


    case OBJMODE_BETAPH2_v26:
      objPoint     = (char*)"ObjectPoint";
      objUnit      = (char*)"ObjectUnit";
      objDoodad    = (char*)"ObjectDoodad";
      objGroup     = (char*)"Group";
      attName      = (char*)"Name";
      attType      = (char*)"Type";
      attPosition  = (char*)"Position";
      attRotation  = (char*)"Rotation";
      attUnitType  = (char*)"UnitType";
      attResources = (char*)"Resources";
      chdFlag      = (char*)"Flag";
      flgIndex     = (char*)"Index";
      flgValue     = (char*)"Value";
      poWatchtower = (char*)"XelNagaTower";
      poRichMineralField = (char*)"RichMineralField";

      objsToIgnore.push_back( "Observatory" );
      objsToIgnore.push_back( "HighYieldMineralField" );
    break;


    default:
      printError( "Unknown object processing mode?\n" );
      exit( -1 );
  }

  // ignore groups, don't affect analysis
  if( strcmp( object->Value(), objGroup ) == 0 )
  {
    return;
  }



  // everything has a position, right?
  float mx, my, mz;
  const char* strPosition = object->Attribute( attPosition );
  if( strPosition == NULL ) {
    const char* strID = object->Attribute( "id" );
    printError( "PlacedObject id=%s has no position.\n", strID );
  }
  sscanf( strPosition, "%f,%f,%f", &mx, &my, &mz );



  float rot;
  const char* strRotation = object->Attribute( attRotation );
  if( strRotation == NULL ) {
    // not everything has an explicit rotation, default to zero
    rot = 0.0f;
  } else {
    sscanf( strRotation, "%f", &rot );
  }
  // for some reason the angle in degrees used for placed objects
  // within the editor is not what gets written into the Objects
  // file--it gets written as radians AND shifted by PI/2!!
  // convert back to degrees to match editor, easier
  rot = (rot - 0.5f*float(M_PI)) * 180.0f / float(M_PI);
  if( rot < 0.0f ) {
    rot = 360.0f + rot;
  }



  // Points might be a start location
  if( strcmp( object->Value(), objPoint ) == 0 )
  {
    const char* strType = object->Attribute( attType );


    if( strcmp( strType, "StartLoc" ) == 0 )
    {
      StartLoc* sl = new StartLoc;
      sl->loc.mSet( mx, my );
      startLocs.push_back( sl );
      return;
    }


    if( strcmp( strType, "BlockPathing" ) == 0 )
    {
      point c;
      c.mSet( mx, my );
      pathingFills.push_back( c );
      return;
    }


    // just points placed on map
    if( strcmp( strType, "Normal" ) == 0 )
    {
      const char* strName = object->Attribute( attName );

      if( strncmp( strName, "testDetectChoke", 15 ) == 0 )
      {
        point c;
        c.mSet( mx, my );
        opennessNeighborhoodsToRender.push_back( c );
        return;
      }
    }
  }



  // doodads might have a footprint, don't worry about scaled
  // doodads--their footprint is the same
  if( strcmp( object->Value(), objDoodad ) == 0 )
  {
    // check if the "don't use footprint" flag of this doodad
    // is set, which means we can ignore it
    for( TiXmlElement* flag = object->FirstChildElement( chdFlag );
         flag;
         flag = flag->NextSiblingElement( chdFlag ) )
    {
      const char* indexStr = flag->Attribute( flgIndex );
      const char* valueStr = flag->Attribute( flgValue );

      if( indexStr != NULL &&
          valueStr != NULL &&
          strcmp( indexStr, "NoDoodadFootprint" ) == 0 &&
          strcmp( valueStr, "1"                 ) == 0 )
      {
        return;
      }
    }

    // otherwise find appropriate footprint
    const char* strType = object->Attribute( attType );

    string name( strType );

    FootToApply fta;
    fta.loc.mSet( mx, my );
    fta.rot = rot;
	fta.type = FP_DOODAD | FP_NOBUILD | FP_LOSB;
    fta.name.assign( name );
    footsToApply.push_back( fta );

    return;
  }



  // units come in three flavors:
  //   resource - can be mined, has a footprint
  //   destruct - can be destroyed, has a footprint
  //   unit     - may have a footprint

  if( strcmp( object->Value(), objUnit ) == 0 )
  {
    // every unit has a unitType (TRUE?)
    const char* strType = object->Attribute( attUnitType );

    // might be used by resources below
    const char* strResources = object->Attribute( attResources );


    string name( strType );


    for( list<string>::iterator itr = objsToIgnore.begin();
         itr != objsToIgnore.end();
         ++itr )
    {
      if( name == *itr )
      {
        return;
      }
    }


    // an object may have more than one type of footprint,
    // all of which will be applied
      FootToApply fta;
 
    fta.loc.mSet( mx, my );
    fta.rot = rot;
	fta.type = FP_DESTRUCT | FP_UNIT | FP_RESOURCE | FP_NOBUILDMAIN |
		FP_NOBUILD | FP_LOSB | FP_COLLAPSIBLE_SOURCE | FP_COLLAPSIBLE_TARGET;
    fta.name.assign( name );
    footsToApply.push_back( fta );

    if( strcmp( strType, poWatchtower ) == 0 )
    {
      Watchtower* wt = new Watchtower;
      wt->loc.mSet( mx, my );
      wt->range = 22.0f;
      watchtowers.push_back( wt );
      return;
    }

    if( strcmp( strType, "MineralField" ) == 0 ||
		strcmp( strType, "PurifierMineralField") == 0 ||
		strcmp( strType, "PurifierMineralField750") == 0 )
    {
      Resource* r = new Resource;
      r->loc.mSet( mx, my );
      r->cliffLevel = getHeight( &(r->loc) );
      r->type       = MINERALS;
      if( strResources != NULL )
      {
        r->amount = atof( strResources );
      } else {
        r->amount = getfConstant( "defaultMineralAmount" );
      }
      resources.push_back( r );
      return;
    }

    if( strcmp( strType, poRichMineralField ) == 0 ||
		strcmp(strType, "PurifierRichMineralField" ) == 0 ||
		strcmp(strType, "PurifierRichMineralField750" ) == 0 )
    {
      Resource* r = new Resource;
      r->loc.mSet( mx, my );
      r->cliffLevel = getHeight( &(r->loc) );
      r->type       = MINERALS_HY;
      if( strResources != NULL )
      {
        r->amount = atof( strResources );
      } else {
        r->amount = getfConstant( "defaultMineralAmount" );
      }
      resources.push_back( r );
      return;
    }

    if( strcmp( strType, "VespeneGeyser"        ) == 0 ||
        strcmp( strType, "SpacePlatformGeyser"  ) == 0 ||
		strcmp( strType, "ProtossVespeneGeyser") == 0 ||
		strcmp( strType, "PurifierVespeneGeyser") == 0 )
    {
      Resource* r = new Resource;
      r->loc.mSet( mx, my );
      r->cliffLevel = getHeight( &(r->loc) );
      r->type       = VESPENEGAS;
      if( strResources != NULL )
      {
        r->amount = atof( strResources );
      } else {
        r->amount = getfConstant( "defaultGeyserAmount" );
      }
      resources.push_back( r );
      return;
    }

    if( strcmp( strType, "RichVespeneGeyser" ) == 0 )
    {
      Resource* r = new Resource;
      r->loc.mSet( mx, my );
      r->cliffLevel = getHeight( &(r->loc) );
      r->type       = VESPENEGAS_HY;
      if( strResources != NULL )
      {
        r->amount = atof( strResources );
      } else {
        r->amount = getfConstant( "defaultGeyserAmount" );
      }
      resources.push_back( r );
      return;
    }

	OutputDebugString(strType);
  }
}


// From editor testing:
// dynamic pathing fills flow across ramps!
// they do NOT flow across painted green pathing
// they DO flow across (ignore) doodads
// THEREFORE, the dynamic fills should be executed before
// any placed objects modify the pathing map made from
// the terrain only
//
// SPECIAL NOTE: as if this wasn't already complicated--this
// tool already applies the painted pathing layer BEFORE doing
// placed objects so we can tell when resources are in unpathable
// areas, which means we can't do dynamic fills BEFORE painted
// pathing is applied.  The way to really handle this is to keep
// painted pathing as a separate layer up to this point and apply
// after, then to do resources you check if they are in the
// terrain pathing OR in the painted pathing layer
void SC2Map::applyFillsAndFootprints()
{
  for( list<point>::iterator itr = pathingFills.begin();
       itr != pathingFills.end();
       ++itr )
  {
    applyFill( &(*itr) );
  }
  pathingFills.clear();

  list<string> unfoundFootprints;
  for( list<FootToApply>::iterator itr = footsToApply.begin();
       itr != footsToApply.end();
       ++itr )
  {
    FootToApply* fta = &(*itr);
    bool footPlaced = applyFootprint( &(fta->loc), fta->rot, fta->type, &(fta->name) );
	if (footPlaced == false)
	{
		if (find(unfoundFootprints.begin(), unfoundFootprints.end(), fta->name) == unfoundFootprints.end())
			unfoundFootprints.push_back(fta->name);
	}
  }
  footsToApply.clear();

  if (unfoundFootprints.size())
  {
	  printError("%i missing footprints:", unfoundFootprints.size());
	  for (list<string>::iterator itr = unfoundFootprints.begin();
			itr != unfoundFootprints.end();
			 ++itr)
	  {
		  printError("-   %s", (*itr).c_str());
	  }
  }
}



void SC2Map::applyFill( point* src )
{
  if( !isPlayableCell( src ) )
  {
    return;
  }

  pathingFillsToRender.push_back( *src );

  map<int, point> fillSet;
  map<int, point> workSet;

  propagateFill(  0,  0, src, &fillSet, &workSet );

  while( !workSet.empty() )
  {
    map<int, point>::iterator itr = workSet.begin();
    point c = itr->second;
    workSet.erase( makePointKey( &c ) );

    fillSet[makePointKey( &c )] = c;

    // DON'T PROPAGATE DIAGONALLY WITH NEW PATHING?
    //propagateFill(  1,  1, &c, &fillSet, &workSet );
    //propagateFill(  1, -1, &c, &fillSet, &workSet );
    //propagateFill( -1, -1, &c, &fillSet, &workSet );
    //propagateFill( -1,  1, &c, &fillSet, &workSet );

    propagateFill(  1,  0, &c, &fillSet, &workSet );
    propagateFill(  0, -1, &c, &fillSet, &workSet );
    propagateFill( -1,  0, &c, &fillSet, &workSet );
    propagateFill(  0,  1, &c, &fillSet, &workSet );
  }


  for( map<int, point>::iterator itr = fillSet.begin();
       itr != fillSet.end();
       ++itr )
  {
    point c = itr->second;

    for( int t = 0; t < NUM_PATH_TYPES; ++t )
    {
      setPathing( &c, (PathType)t, false );
    }
  }
}


int SC2Map::makePointKey( point* c )
{
  return c->pcx + c->pcy*cxDimPlayable;
}


void SC2Map::propagateFill( int dx, int dy, point* c, map<int, point>* fillSet, map<int, point>* workSet )
{
  point cn;
  cn.pcSet( c->pcx + dx, c->pcy + dy );

  if( !isPlayableCell( &cn ) )
  {
    return;
  }

  if( !getPathing( &cn, PATH_GROUND_NOROCKS ) )
  {
    return;
  }

  int k = makePointKey( &cn );

  map<int, point>::iterator itr = fillSet->find( k );

  if( itr == fillSet->end() )
  {
    (*workSet)[k] = cn;
  }
}


Footprint* SC2Map::findFootprint( float rot, string* name, int fp_type)
{
	Footprint* foot = NULL;
	map<string, Footprint*>* innerMap;
	string specificRot(*name);

	if (rot > 45.01f && rot <= 135.01f) {
		specificRot.append(footprintRot270cw);

	}
	else if (rot > 135.01f && rot <= 225.01f) {
		specificRot.append(footprintRot180cw);

	}
	else if (rot > 225.01f && rot <= 315.01f) {
		specificRot.append(footprintRot90cw);
	}

	// first check local user config
	innerMap = getInnerMap(&configUserLocal, fp_type);
	if (innerMap != NULL)
	{
		foot = (*innerMap)[specificRot];

		if (foot == NULL) {
			foot = (*innerMap)[*name];
		}
	}

	// if nothing there, check global
	if (foot == NULL)
	{
		innerMap = getInnerMap(&configUserGlobal, fp_type);
		if (innerMap != NULL)
		{
			foot = (*innerMap)[specificRot];

			if (foot == NULL) {
				foot = (*innerMap)[*name];
			}
		}
	}
		
	return foot;
}



bool SC2Map::applyFootprint( point* c, float rot, int type, string* name )
{
	bool placement = false;

	for (int i = 0; i < FootprintTypesSize; ++i) // HACK
	{
		FootprintTypes fp_type = (FootprintTypes)(1 << i);

		if (type & fp_type)
		{
			Footprint* foot = findFootprint(rot, name, fp_type);
			if (foot == NULL)  foot = findFootprint(0, name, fp_type);
			if (foot == NULL) continue;

			placement = true;

			// if there is a footprint for the specific
			// rotation, use that, otherwise default to
			// the one footprint
			string specificRot(*name);
			int offX = 0;
			int offY = 0;
			if (fp_type == FP_COLLAPSIBLE_SOURCE || fp_type == FP_COLLAPSIBLE_SOURCE)
			{
				if (fp_type == FP_COLLAPSIBLE_SOURCE)
				{
					if (rot > 44.f && rot < 46.f)
					{
						offX = 4;
						offY = 4;
					}
					else if (rot > 134.f && rot < 136.f)
					{
						offX = -4;
						offY = 4;
					}
					if (rot > 224.f && rot < 226.f)
					{
						offX = -4;
						offY = -4;
					}
					else if (rot > 314.f && rot < 316.f)
					{
						offX = 4;
						offY = -4;
					}
				}
			}


			for (list<int>::iterator itr = foot->relativeCoordinates.begin();
			itr != foot->relativeCoordinates.end();
				++itr)
			{
				int dx = *itr;

				// there should be an even number of coordinates!
				++itr;
				if (itr == foot->relativeCoordinates.end())
				{
					//printError( "Odd number of coordinates for footprint %s %s.\n",
					//            type->data(),
					//            name->data() );
					exit(-1);
				}

				int dy = *itr;

				point dc;
				dc.mSet(c->mx + dx + offX + 0.25f, c->my + dy + offY + 0.25f);

				if (!isPlayableCell(&dc))
				{
					continue;
				}

				if (fp_type == FP_DOODAD || fp_type == FP_UNIT) {
					// remove all pathing
					setPathing(&dc, PATH_GROUND_NOROCKS, false);
					setPathing(&dc, PATH_GROUND_WITHROCKS, false);
					setPathing(&dc, PATH_CWALK_NOROCKS, false);
					setPathing(&dc, PATH_CWALK_WITHROCKS, false);
					setPathing(&dc, PATH_GROUND_WITHROCKS_NORESOURCES, false);
					setPathing(&dc, PATH_BUILDABLE, false);
					setPathing(&dc, PATH_BUILDABLE_MAIN, false);

				}
				else if (fp_type == FP_DESTRUCT) {
					// remove from WITHROCKS pathing types
					setPathing(&dc, PATH_GROUND_WITHROCKS, false);
					setPathing(&dc, PATH_CWALK_WITHROCKS, false);
					setPathing(&dc, PATH_GROUND_WITHROCKS_NORESOURCES, false);
					Destruct* destruct = new Destruct;
					destruct->loc.set(&dc);
					destructs.push_back(destruct);

				}
				else if (fp_type == FP_COLLAPSIBLE_SOURCE) {
					// remove from WITHROCKS pathing types
					setPathing(&dc, PATH_GROUND_WITHROCKS, false);
					setPathing(&dc, PATH_CWALK_WITHROCKS, false);
					setPathing(&dc, PATH_GROUND_WITHROCKS_NORESOURCES, false);
					Collapsible* collapsible = new Collapsible;
					collapsible->loc.set(&dc);
					collapsible->state = true;
					collapsibles.push_back(collapsible);

				}
				else if (fp_type == FP_COLLAPSIBLE_TARGET) {
					// 
					setPathing(&dc, PATH_BUILDABLE, false);
					setPathing(&dc, PATH_BUILDABLE_MAIN, false);
					Collapsible* collapsible = new Collapsible;
					collapsible->loc.set(&dc);
					collapsible->state = false;
					collapsibles.push_back(collapsible);

				}
				else if (fp_type == FP_RESOURCE) {
					// remove all pathing except WITHOUTRESOURCES
					setPathing(&dc, PATH_GROUND_NOROCKS, false);
					setPathing(&dc, PATH_GROUND_WITHROCKS, false);
					setPathing(&dc, PATH_CWALK_NOROCKS, false);
					setPathing(&dc, PATH_CWALK_WITHROCKS, false);
					setPathing(&dc, PATH_BUILDABLE, false);
					setPathing(&dc, PATH_BUILDABLE_MAIN, false);

				}
				else if (fp_type ==  FP_NOBUILDMAIN) {
					setPathing(&dc, PATH_BUILDABLE_MAIN, false);

				}
				else if (fp_type == FP_NOBUILD) {
					setPathing(&dc, PATH_BUILDABLE, false);
					setPathing(&dc, PATH_BUILDABLE_MAIN, false);

				}
				else if (fp_type == FP_LOSB) {
					setPathing(&dc, PATH_BUILDABLE, false);
					setPathing(&dc, PATH_BUILDABLE_MAIN, false);
					LoSB* losb = new LoSB;
					losb->loc.set(&dc);
					losbs.push_back(losb);
				}
				else {
					printError("Placed object %s has unknown type %i.",
						name->data(),
						type);
					exit(-1);
				}
			}
		}
	}

  return placement;
}
