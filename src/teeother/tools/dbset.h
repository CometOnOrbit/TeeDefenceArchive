#ifndef GAME_SERVER_MMOCORE_UTILS_DBSET_H
#define GAME_SERVER_MMOCORE_UTILS_DBSET_H

#include <string>
#include <algorithm>
#include <sstream>

class DBSet
{
	std::vector<std::string> m_DataItems {};

public:
	// constructors
	DBSet() = default;
	DBSet(std::string_view Data)
	{
		Init(Data);
	}

	// operator =
	DBSet& operator=(std::string_view set)
	{
		DBSet tmp(set);
		*this = std::move(tmp);
		return *this;
	}

	// emplace function
	template <typename... Args>
	void emplace(Args&&... args)
	{
		m_DataItems.emplace_back(std::forward<Args>(args)...);
	}

	// sort items, default by less
	void sortItems(std::function<bool(const std::string&, const std::string&)> comparator = std::less<>())
	{
		std::sort(m_DataItems.begin(), m_DataItems.end(), comparator);
	}

	// dump string
	std::string dump() const
	{
		if(m_DataItems.empty())
			return "";

		std::string result = m_DataItems[0];
		for(size_t i = 1; i < m_DataItems.size(); ++i)
			result += "," + m_DataItems[i];
		return result;
	}

	bool hasSet(const std::string& pSet) const { return std::find(m_DataItems.begin(), m_DataItems.end(), pSet) != m_DataItems.end(); }
	const std::vector<std::string>& getItems() const { return m_DataItems; }

private:
	void Init(std::string_view TmpData)
	{
		if(TmpData.empty())
			return;

		const char delimiter = ',';
		std::istringstream stream(std::string(TmpData));
		std::string token;
		while(std::getline(stream, token, delimiter))
		{
			token.erase(token.find_last_not_of(' ') + 1);
			token.erase(0, token.find_first_not_of(' '));

			if(!token.empty())
				m_DataItems.push_back(token);
		}
	}
};


#endif //GAME_SERVER_MMO_UTILS_DBSET_H
