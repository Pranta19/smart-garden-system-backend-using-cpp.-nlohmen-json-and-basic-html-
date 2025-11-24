#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <ctime>
#include <iomanip>
#include <algorithm>

// --- MANDATORY REQUIREMENT: json.hpp must be in the same folder ---
#include "nlohmann.h"

using json = nlohmann::json;

// ==================== UTILITY FUNCTIONS ====================

// Helper: Adds a number of days to a YYYY-MM-DD date string.
std::string add_days(const std::string &ymd, int days) {
    std::tm tm = {};
    std::istringstream ss(ymd);
    
    if (!(ss >> std::get_time(&tm, "%Y-%m-%d"))) return "invalid-date";
    
    std::time_t t = std::mktime(&tm);
    t += days * 24 * 3600; 
    
    std::tm *tm2 = std::localtime(&t);
    char buf[11];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", tm2);
    return std::string(buf);
}

// ==================== OBJECT STRUCTURES ====================

// 1. CareEvent: Represents a single historical care action.
struct CareEvent {
    std::string type; 
    std::string date; 
    std::string notes;

    std::string serialize() const {
        return "EVENT|" + type + "|" + date + "|" + notes;
    }

    static CareEvent deserialize(const std::string &line) {
        CareEvent e;
        std::stringstream ss(line);
        std::string token;
        
        getline(ss, token, '|'); // Skip "EVENT" tag
        getline(ss, e.type, '|');
        getline(ss, e.date, '|');
        getline(ss, e.notes, '\n'); 
        
        return e;
    }
};

// 2. Schedule: Contains the recurring logic for an action.
struct Schedule {
    int interval_days = 0;

    // CORE SMART LOGIC: Computes the next scheduled date
    std::string computeNext(const std::vector<CareEvent>& history, const std::string &type) const {
        if (interval_days <= 0) return "Manual Care";

        std::string last_date = "";
        
        for (auto it = history.rbegin(); it != history.rend(); ++it) {
            if (it->type == type) { 
                last_date = it->date; 
                break; 
            }
        }

        if (last_date.empty()) return "due now"; 
        
        return add_days(last_date, interval_days);
    }

    std::string serialize() const { return std::to_string(interval_days); }
    
    static Schedule deserialize(const std::string &s) {
        Schedule sch; 
        try { sch.interval_days = std::stoi(s); } catch(...) { sch.interval_days = 0; }
        return sch;
    }
};

// 3. Plant: The core data model object.
struct Plant {
    int id;
    std::string name;
    std::string species;
    std::string planted;
    int pot_size_cm;
    std::string sunlight;
    
    Schedule watering;
    Schedule fertilizer;
    std::vector<CareEvent> history;

    std::string serialize() const {
        std::stringstream ss;
        ss << "PLANT|" << id << "|" << name << "|" << species << "|" << planted << "|"
           << pot_size_cm << "|" << sunlight << "|" 
           << watering.serialize() << "|" << fertilizer.serialize();
        return ss.str();
    }

    static Plant deserialize(const std::string &line, std::vector<std::string> &followingLines) {
        Plant p;
        std::stringstream ss(line);
        std::string token;
        
        getline(ss, token, '|'); // Skip "PLANT"
        getline(ss, token, '|'); p.id = std::stoi(token);
        getline(ss, p.name, '|');
        getline(ss, p.species, '|');
        getline(ss, p.planted, '|');
        getline(ss, token, '|'); p.pot_size_cm = std::stoi(token);
        getline(ss, p.sunlight, '|');
        getline(ss, token, '|'); p.watering = Schedule::deserialize(token);
        getline(ss, token, '|'); p.fertilizer = Schedule::deserialize(token);

        // Consume event lines attached to this plant
        for (size_t i = 0; i < followingLines.size();) {
            if (followingLines[i].find("EVENT|") == 0) {
                p.history.push_back(CareEvent::deserialize(followingLines[i]));
                followingLines.erase(followingLines.begin() + i);
            } else {
                break; 
            }
        }
        return p;
    }
};

// 4. Garden: Manager class for plants and file I/O.
struct Garden {
    std::vector<Plant> plants;
    int next_id = 1;

    int addPlant(const Plant &p) {
        Plant q = p; 
        q.id = next_id++; 
        plants.push_back(q); 
        return q.id;
    }

    Plant* findPlant(int id) {
        for (auto &p : plants) if (p.id == id) return &p;
        return nullptr;
    }

    bool saveToFile(const std::string &fname) {
        std::ofstream f(fname);
        if (!f.is_open()) return false;
        for (const auto &p : plants) {
            f << p.serialize() << "\n";
            for (const auto &e : p.history) f << e.serialize() << "\n";
        }
        return true;
    }

    bool loadFromFile(const std::string &fname) {
        std::ifstream f(fname);
        if (!f.is_open()) return false; 
        
        std::vector<std::string> lines; 
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back(); 
            if (!line.empty()) lines.push_back(line);
        }
        
        plants.clear(); 
        next_id = 1;
        size_t i = 0;
        
        while (i < lines.size()) {
            if (lines[i].find("PLANT|") == 0) {
                std::vector<std::string> rest;
                for (size_t j = i + 1; j < lines.size(); ++j) rest.push_back(lines[j]);
                
                Plant p = Plant::deserialize(lines[i], rest);
                
                size_t consumed = lines.size() - (i + 1) - rest.size();
                
                for (size_t k = 0; k < consumed; k++) lines.erase(lines.begin() + i + 1);
                
                plants.push_back(p);
                i++; 
                if (p.id >= next_id) next_id = p.id + 1;
            } else {
                i++;
            }
        }
        return true;
    }
};

// ==================== CGI HANDLER ====================

void outputJson(const Garden &garden) {
    json j;
    j["ok"] = true;
    j["plants"] = json::array();
    
    for (const auto &p : garden.plants) {
        json jp;
        jp["id"] = p.id;
        jp["name"] = p.name;
        jp["species"] = p.species;
        jp["planted"] = p.planted;
        jp["pot_size_cm"] = p.pot_size_cm;
        jp["sunlight"] = p.sunlight;
        jp["watering_interval"] = p.watering.interval_days;
        jp["fertilizer_interval"] = p.fertilizer.interval_days;
        
        // **SMART LOGIC OUTPUT**
        jp["next_water"] = p.watering.computeNext(p.history, "water");
        jp["next_fertilize"] = p.fertilizer.computeNext(p.history, "fertilize");
        
        jp["history"] = json::array();
        for (const auto &e : p.history) {
            jp["history"].push_back({
                {"type", e.type},
                {"date", e.date},
                {"notes", e.notes}
            });
        }
        j["plants"].push_back(jp);
    }
    
    std::cout << "Content-Type: application/json\r\n\r\n";
    std::cout << j.dump(4);
}

int main() {
    Garden garden;
    garden.loadFromFile("garden.txt"); 

    char* q_env = getenv("QUERY_STRING");
    std::string query = q_env ? q_env : "";
    
    char* m_env = getenv("REQUEST_METHOD");
    std::string method = m_env ? m_env : "";

    // --- Action: list (GET) ---
    if (query.find("action=list") != std::string::npos) {
        outputJson(garden);
    }
    // --- Actions: add or log (POST) ---
    else if (method == "POST") {
        std::string body;
        std::getline(std::cin, body, '\0');

        auto findVal = [&](const std::string &key) {
            std::string search = key + "=";
            size_t pos = body.find(search);
            if (pos == std::string::npos) return std::string("");
            
            size_t start = pos + search.size();
            size_t end = body.find("&", start);
            if (end == std::string::npos) end = body.size();
            
            std::string val = body.substr(start, end - start);
            
            for (char &c : val) {
                if (c == '+') c = ' ';
            }
            return val;
        };

        // Action: add new plant
        if (body.find("action=add") != std::string::npos) {
            Plant p;
            p.name = findVal("name");
            p.species = findVal("species");
            p.planted = findVal("planted");
            try { p.pot_size_cm = std::stoi(findVal("pot_size_cm")); } catch(...) { p.pot_size_cm = 0; }
            p.sunlight = findVal("sunlight");
            try { p.watering.interval_days = std::stoi(findVal("watering_interval")); } catch(...) { p.watering.interval_days = 0; }
            try { p.fertilizer.interval_days = std::stoi(findVal("fertilizer_interval")); } catch(...) { p.fertilizer.interval_days = 0; }
            
            garden.addPlant(p);
            garden.saveToFile("garden.txt");
            
            std::cout << "Content-Type: application/json\r\n\r\n";
            std::cout << R"({"ok":true})";
        }
        // Action: log a care event (water or fertilize)
        else if (body.find("action=log") != std::string::npos) {
            int id = 0;
            try { id = std::stoi(findVal("id")); } catch(...) {}
            
            Plant* p = garden.findPlant(id);
            if (p) {
                CareEvent e;
                e.type = findVal("type");
                e.date = findVal("date");
                e.notes = findVal("notes");
                p->history.push_back(e);
                garden.saveToFile("garden.txt");
            }
            
            std::cout << "Content-Type: application/json\r\n\r\n";
            std::cout << R"({"ok":true})";
        }
    }
    else {
        std::cout << "Content-Type: text/plain\r\n\r\n";
        std::cout << "Smart Garden CGI Interface Ready. Use index.html to access the API.";
    }

    return 0;
}