/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_MAPITEMS_H
#define GAME_MAPITEMS_H

// layer types
enum
{
	LAYERTYPE_INVALID = 0,
	LAYERTYPE_GAME,
	LAYERTYPE_TILES,
	LAYERTYPE_QUADS,
	LAYERTYPE_FRONT,
	LAYERTYPE_TELE,
	LAYERTYPE_SPEEDUP,
	LAYERTYPE_SWITCH,
	LAYERTYPE_TUNE,
	LAYERTYPE_SOUNDS = 10,

	MAPITEMTYPE_VERSION = 0,
	MAPITEMTYPE_INFO,
	MAPITEMTYPE_IMAGE,
	MAPITEMTYPE_ENVELOPE,
	MAPITEMTYPE_GROUP,
	MAPITEMTYPE_LAYER,
	MAPITEMTYPE_ENVPOINTS,
	MAPITEMTYPE_SOUND,

	CURVETYPE_STEP = 0,
	CURVETYPE_LINEAR,
	CURVETYPE_SLOW,
	CURVETYPE_FAST,
	CURVETYPE_SMOOTH,
	CURVETYPE_BEZIER,
	NUM_CURVETYPES,

	// game layer tiles — TDA (Teeworlds original)
	ENTITY_NULL = 0,
	ENTITY_SPAWN,
	ENTITY_SPAWN_RED,
	ENTITY_SPAWN_BLUE,
	ENTITY_FLAGSTAND_RED,
	ENTITY_FLAGSTAND_BLUE,
	ENTITY_ARMOR_1,
	ENTITY_HEALTH_1,
	ENTITY_WEAPON_SHOTGUN,
	ENTITY_WEAPON_GRENADE,
	ENTITY_POWERUP_NINJA,
	ENTITY_WEAPON_LASER,
	ENTITY_LOG,
	ENTITY_COAL,
	ENTITY_COPPER,
	ENTITY_IRON,
	ENTITY_GOLD,
	ENTITY_DIAMOND,
	ENTITY_MAIN_TOWER,
	ENTITY_ENERGY,
	NUM_ENTITIES,

	// tiles — TDA / DDNet base
	TILE_AIR = 0,
	TILE_HOOKABLE,  // =1
	TILE_DEATH,     // =2
	TILE_UNHOOKABLE, // =3 (identical to TILE_NOHOOK)
	TILE_WATER = 6,

	// tiles — TDA MMO extras
	TILE_TELE_FROM_CONFIRM = 10,
	TILE_SW_ZONE = 22,
	TILE_TELE_FROM = 26,
	TILE_TELE_OUT = 27,
	TILE_SHOP_ZONE = 29,
	TILE_CHAIR_LV1 = 33,
	TILE_CHAIR_LV2 = 34,
	TILE_CHAIR_LV3 = 35,
	TILE_NPC_INTERACT = 40,
	TILE_INFO_ZONE = 41,

	// tiles — MRPG / DDNet extended (additions, no TDA conflict)
	TILE_SOLID = 1,          // alias for TILE_HOOKABLE
	TILE_NOHOOK = 3,          // alias for TILE_UNHOOKABLE
	TILE_FIXED_CAM = 4,
	TILE_SMOOTH_FIXED_CAM = 5,
	TILE_PLAYER_HOUSE = 8,
	TILE_DESTROYER_PROJECTILE = 11,
	TILE_WORLD_SWAPPER = 14,
	TILE_JAIL_ZONE = 15,
	TILE_GUILD_HOUSE = 16,
	TILE_AUCTION = 17,
	TILE_AETHER_TELEPORT = 28,
	TILE_CRAFT_ZONE = 31,
	TILE_GUILD_CHAIR = 32,
	TILE_BANK_MANAGER = 38,
	TILE_FISHING_MODE = 39,
	TILE_QUEST_BOARD = 42,
	TILE_STOP = 60,
	TILE_STOPS,
	TILE_STOPA,
	TILE_SW_TEXT = 70,
	TILE_SW_HOUSE_ZONE = 71,
	TILE_SW_ACTION_ZONE = 72,

	MAX_TILES = 255,

	TILEFLAG_VFLIP = 1,
	TILEFLAG_HFLIP = 2,
	TILEFLAG_OPAQUE = 4,
	TILEFLAG_ROTATE = 8,
	ROTATION_0 = 0,
	ROTATION_90 = TILEFLAG_ROTATE,
	ROTATION_180 = (TILEFLAG_VFLIP | TILEFLAG_HFLIP),
	ROTATION_270 = (TILEFLAG_VFLIP | TILEFLAG_HFLIP | TILEFLAG_ROTATE),

	LAYERFLAG_DETAIL = 1,
	TILESLAYERFLAG_GAME = 1,
	TILESLAYERFLAG_TELE = 2,
	TILESLAYERFLAG_SPEEDUP = 4,
	TILESLAYERFLAG_FRONT = 8,
	TILESLAYERFLAG_SWITCH = 16,

	ENTITY_OFFSET = 255 - 16 * 4,
};

struct CPoint
{
	int x, y; // 22.10 fixed point
};

struct CColor
{
	int r, g, b, a;
};

struct CQuad
{
	CPoint m_aPoints[5];
	CColor m_aColors[4];
	CPoint m_aTexcoords[4];

	int m_PosEnv;
	int m_PosEnvOffset;

	int m_ColorEnv;
	int m_ColorEnvOffset;
};

class CTile
{
public:
	unsigned char m_Index;
	unsigned char m_Flags;
	unsigned char m_Skip;
	unsigned char m_Reserved;
};

class CTeleTile
{
public:
	unsigned char m_Number;
	unsigned char m_Type;
};

class CSwitchTileExtra
{
public:
	unsigned char m_Number;
	unsigned char m_Type;
	unsigned char m_Flags;
	unsigned char m_Delay;
};

class CSpeedupTileExtra
{
public:
	unsigned char m_Force;
	unsigned char m_MaxSpeed;
	unsigned char m_Type;
	short m_Angle;
};

struct CMapItemInfo
{
	int m_Version;
	int m_Author;
	int m_MapVersion;
	int m_Credits;
	int m_License;
};

struct CMapItemInfoSettings : CMapItemInfo
{
	int m_Settings;
};

struct CMapItemImage_v1
{
	int m_Version;
	int m_Width;
	int m_Height;
	int m_External;
	int m_ImageName;
	int m_ImageData;
};

struct CMapItemImage : public CMapItemImage_v1
{
	enum
	{
		CURRENT_VERSION = 2
	};
	int m_MustBe1;
};

struct CMapItemGroup_v1
{
	int m_Version;
	int m_OffsetX;
	int m_OffsetY;
	int m_ParallaxX;
	int m_ParallaxY;

	int m_StartLayer;
	int m_NumLayers;
};

struct CMapItemGroup : public CMapItemGroup_v1
{
	enum
	{
		CURRENT_VERSION = 3
	};

	int m_UseClipping;
	int m_ClipX;
	int m_ClipY;
	int m_ClipW;
	int m_ClipH;

	int m_aName[3];
};

struct CMapItemLayer
{
	int m_Version;
	int m_Type;
	int m_Flags;
};

struct CMapItemLayerTilemap
{
	enum
	{
		CURRENT_VERSION = 3,
		TILE_SKIP_MIN_VERSION = 4,
	};

	CMapItemLayer m_Layer;
	int m_Version;

	int m_Width;
	int m_Height;
	int m_Flags;

	CColor m_Color;
	int m_ColorEnv;
	int m_ColorEnvOffset;

	int m_Image;
	int m_Data;

	int m_aName[3];

	// DDRace extended fields
	int m_Tele;
	int m_Speedup;
	int m_Front;
	int m_Switch;
	int m_Tune;
};

struct CMapItemLayerQuads
{
	enum
	{
		CURRENT_VERSION = 2
	};

	CMapItemLayer m_Layer;
	int m_Version;

	int m_NumQuads;
	int m_Data;
	int m_Image;

	int m_aName[3];
};

struct CMapItemVersion
{
	enum
	{
		CURRENT_VERSION = 1
	};

	int m_Version;
};

struct CEnvPoint_v1
{
	int m_Time; // in ms
	int m_Curvetype;
	int m_aValues[4]; // 1-4 depending on envelope (22.10 fixed point)

	bool operator<(const CEnvPoint_v1 &Other) const { return m_Time < Other.m_Time; }
};

struct CEnvPoint : public CEnvPoint_v1
{
	// bezier curve only
	// dx in ms and dy as 22.10 fxp
	int m_aInTangentdx[4];
	int m_aInTangentdy[4];
	int m_aOutTangentdx[4];
	int m_aOutTangentdy[4];

	bool operator<(const CEnvPoint &other) const { return m_Time < other.m_Time; }
};

struct CMapItemEnvelope_v1
{
	int m_Version;
	int m_Channels;
	int m_StartPoint;
	int m_NumPoints;
	int m_aName[8];
};

struct CMapItemEnvelope_v2 : public CMapItemEnvelope_v1
{
	enum
	{
		CURRENT_VERSION = 2
	};
	int m_Synchronized;
};

struct CMapItemEnvelope : public CMapItemEnvelope_v2
{
	// bezier curve support
	enum
	{
		CURRENT_VERSION = 3
	};
};

class CSoundShape
{
public:
	enum
	{
		SHAPE_RECTANGLE = 0,
		SHAPE_CIRCLE,
		NUM_SHAPES,
	};

	class CRectangle
	{
	public:
		int m_Width, m_Height; // fxp 22.10
	};

	class CCircle
	{
	public:
		int m_Radius;
	};

	int m_Type;

	union
	{
		CRectangle m_Rectangle;
		CCircle m_Circle;
	};
};

class CSoundSource
{
public:
	CPoint m_Position;
	int m_Loop;
	int m_Pan; // 0 - no panning, 1 - panning
	int m_TimeDelay; // in s
	int m_Falloff; // [0,255] // 0 - No falloff, 255 - full

	int m_PosEnv;
	int m_PosEnvOffset;
	int m_SoundEnv;
	int m_SoundEnvOffset;

	CSoundShape m_Shape;
};

class CDoorTile
{
public:
	unsigned char m_Index;
	unsigned char m_Flags;
	int m_Number;
};

class CMapItemLayerSounds
{
public:
	CMapItemLayer m_Layer;
	int m_Version;

	int m_NumSources;
	int m_Data;
	int m_Sound;

	int m_aName[3];
};

class CMapItemSound
{
public:
	int m_Version;

	int m_External;

	int m_SoundName;
	int m_SoundData;
	int m_Unused;
};

#endif
