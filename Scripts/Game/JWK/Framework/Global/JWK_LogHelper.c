/**********************************************************************************************************************
 ************************************************* FREEDOM FIGHTERS ***************************************************
 **********************************************************************************************************************
 * This file is part of the Freedom Fighters project, published under modified APL-ND license. You are free to adapt  *
 * (i.e. modify, rework or update) and share (i.e. copy, distribute or transmit) the material under the following     *
 * conditions:                                                                                                        *
 * - Attribution - You must attribute the material in the manner specified by the author or licensor (but not in any  *
 *   way that suggests that they endorse you or your use of the material).                                            *
 * - Noncommercial - You may not use this material for any commercial purposes.                                       *
 * - Arma Only - You may not convert or adapt this material to be used in other games than Arma.                      *
 * - No Derivatives - If you remix, transform, or build upon the material, you may not distribute the modified        *
 *   material.                                                                                                        *
 * The above list is only a highlight and is NOT an exhaustive list of restrictions. Please visit the following URL   *
 * to view the full text of the license:                                                                              *
 *    https://www.johnnykerner.dev/FreedomFighters/project-license/20250916/                                          *
 **********************************************************************************************************************
 * For more info or to contact the author, visit the project website:                                                 *
 *    https://www.johnnykerner.dev/FreedomFighters/                                                                   *
 ******************************************************************************************************************** */

class JWK_Log
{
	static const int LineWidth = 150;
	static const string TAG = "[JWK]";
	static const string CLASS_PREFIX = "JWK_";
	
	static void Log(string prefix, string what, LogLevel level = LogLevel.NORMAL)
	{
		PrintFormat("%1[%2] %3", TAG, prefix, what, level: level);
	}
	
	static void Log(Managed src, string what, LogLevel level = LogLevel.NORMAL)
	{
		IEntity ent = IEntity.Cast(src);
		
		ScriptComponent comp = ScriptComponent.Cast(src);
		if (comp) ent = comp.GetOwner();
		
		string entityName;
		if (ent && !ent.GetName().IsEmpty() && !JWK_GameMode.Cast(ent))
			entityName = "[" + ent.GetName() + "]";
		
		PrintFormat("%1[%2]%3 %4", TAG, StripClassName(src.ClassName()), entityName, what, level: level);
	}
	
	static void LogDeprecated(Managed src, string what)
	{
		#ifdef WORKBENCH
		Log(src, string.Format(
			"[DEPRECATED] %1 %2",
			what,
			"This will stop working soon! Visit Freedom Fighters website for update instructions."
		), LogLevel.FATAL);
		#else
		Log(src, "[DEPRECATED] " + what, LogLevel.WARNING);
		#endif
	}
	
	static void Log(typename T, string what, LogLevel level = LogLevel.NORMAL)
	{
		PrintFormat("%1[%2] %3", TAG, StripClassName(T.ToString()), what, level: level);
	}
	
	static void Notice()
	{
		string s;
		for (int i = 0; i < LineWidth; i++) s += "=";
		PrintFormat("%1%2", TAG, s);
	}
	
	static void Separator(Managed src, string title)
	{
		string s;
		for (int i = 0, w = LineWidth / 2 - title.Length() / 2 - 1; i < w; i++) s += "-";
		Log(src, string.Format("%1 %2 %3", s, title, s));
	}
	
	static void Separator(string title)
	{
		string s;
		for (int i = 0, w = LineWidth / 2 - title.Length() / 2 - 1; i < w; i++) s += "-";
		
		PrintFormat("%1%2 %3 %4", TAG, s, title, s);
	}
	
	static void DevNotice()
	{
		string s;
		for (int i = 0; i < 150; i++) s += "*";
		for (int y = 0; y < 2; y++) Print(TAG + s);
	}
	
	static string StripClassName(string className)
	{
		if (className.StartsWith(JWK_Log.CLASS_PREFIX)) {
			className = className.Substring(
				JWK_Log.CLASS_PREFIX.Length(),
				className.Length() - JWK_Log.CLASS_PREFIX.Length()
			);
		}
		
		return className;
	}
	
	// --------------------------------------------------------------------------------------
	
	static string Entity(IEntity entity)
	{
		if (!entity) return "null";
		if (!entity.GetName().IsEmpty()) return entity.GetName();
		
		return string.Format("%1 (%2)", JWK_EntityUtils.GetPointerHex(entity), Vector(entity.GetOrigin()));
	}
	
	static string Entity(EntityID entityID)
	{
		IEntity entity = GetGame().GetWorld().FindEntityByID(entityID);
		if (entity) return Entity(entity);
		
		return string.Format("%1 (gone)", entityID.ToString());
	}
	
	static string Vector(vector vec)
	{
		return string.Format("<%1,%2,%3>", vec[0].ToString(-1, 0), vec[1].ToString(-1, 0), vec[2].ToString(-1, 0));
	}
	
	static string VectorXZ(vector vec)
	{
		return string.Format("<%1,%2>", vec[0].ToString(-1, 0), vec[2].ToString(-1, 0));
	}
	
	static string VectorXY(vector vec)
	{
		return string.Format("<%1,%2>", vec[0].ToString(-1, 0), vec[1].ToString(-1, 0));
	}
	
	static string FactionRole(Faction faction)
	{
		if (!faction) return "null";
		return SCR_Enum.GetEnumName(JWK_EFactionRole, JWK.GetFactions().GetRoleByFactionKey(faction.GetFactionKey()));
	}
}

// ---------------------------------------------------------------------------------------------------------

class JWK_LogHelper
{
	bool m_bExtendedContext = false;
	bool m_bEnabled = true;
	int m_iNesting;
	
	string m_Prefix;
	Managed m_Context;
	
	void Notice()
	{
		string s;
		for (int i = 0; i < JWK_Log.LineWidth; i++) s += "=";
		Print(JWK_Log.TAG + s);
	}
	
	void DevNotice()
	{
		string s;
		for (int i = 0; i < JWK_Log.LineWidth; i++) s += "*";
		for (int y = 0; y < 2; y++) Print(JWK_Log.TAG + s);
	}
	
	void Log(string what, LogLevel level = LogLevel.NORMAL)
	{
		if (!m_bEnabled) return;
		
		string fullPrefix = JWK_Log.TAG;
		
		if (m_Context) {
			string prefix;
			if (m_bExtendedContext) prefix = m_Context.ToString();
			else prefix = m_Context.ClassName();
			
			fullPrefix += "[" + JWK_Log.StripClassName(prefix) + "]";
		}
		
		if (!m_Prefix.IsEmpty()) {
			fullPrefix = fullPrefix + "[" + m_Prefix + "]";
		} else {
			ScriptComponent scriptComp = ScriptComponent.Cast(m_Context);
			// dont log entity name for game mode components, redundant as game mode is unique
			if (scriptComp && !JWK_GameModeComponent.Cast(m_Context)) {
				string ownerName = scriptComp.GetOwner().GetName();
				if (!ownerName.IsEmpty())
					fullPrefix = fullPrefix + "[" + scriptComp.GetOwner().GetName() + "]";
			}
		}
		
		string nesting;
		for (int i = 0; i < m_iNesting; i += 1) nesting += "-";
		
		Print(fullPrefix + " " + nesting + what, level);
	}
}

// ---------------------------------------------------------------------------------------------------------

class JWK_LogHelperS
{
	protected typename m_Type;
	
	void JWK_LogHelperS(typename type)
	{
		m_Type = type;
	}
	
	void Log(string what, LogLevel level = LogLevel.NORMAL)
	{
		Print(JWK_Log.TAG + "[" + m_Type.ToString() + "] " + what, level);
	}
}