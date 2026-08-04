#pragma once

#include <boost/property_tree/ptree.hpp>
#include <cstdint>
#include <iostream>
#include <utility>
#include <stdexcept>
#include <string>
#include <map>

namespace xml
{

// ==================== Classes ====================

    class CampofficialPromote
    {
    public:
        CampofficialPromote() = default;
        ~CampofficialPromote() = default;
        CampofficialPromote(const CampofficialPromote&) = delete;
        CampofficialPromote& operator=(const CampofficialPromote&) = delete;
        CampofficialPromote(CampofficialPromote&&) = default;
        CampofficialPromote& operator=(CampofficialPromote&&) = default;

        bool loadXml(const boost::property_tree::ptree& root);
        bool loadXml(const std::string& xml_file_path);
        void dumpAll() const;
        void clear();

        const std::string& getXmlFilePath() const { return xml_file_path_; }


        // --- FuncOpen ---
        class FuncOpen
        {
        public:
            FuncOpen() = default;
            ~FuncOpen() = default;
            FuncOpen(const FuncOpen&) = delete;
            FuncOpen& operator=(const FuncOpen&) = delete;
            FuncOpen(FuncOpen&&) = default;
            FuncOpen& operator=(FuncOpen&&) = default;

            bool loadXml(const boost::property_tree::ptree& root);
            void dumpAll() const;
            void clear();

            uint32_t getOpen() const { return open_; }
            uint32_t getAllZone() const { return allzone_; }
            std::string getOpenZones() const { return openzones_; }
            std::string getAddExpOitendtime() const { return addexpoitendtime_; }
            uint32_t getSeasonAwardOpen() const { return seasonawardopen_; }

        private:
            uint32_t open_ = 0;
            uint32_t allzone_ = 0;
            std::string openzones_ = "";
            std::string addexpoitendtime_ = "";
            uint32_t seasonawardopen_ = 0;
        };

        FuncOpen funcopen;

        // --- ExpLoitlimit ---
        class ExpLoitlimit
        {
        public:
            ExpLoitlimit() = default;
            ~ExpLoitlimit() = default;
            ExpLoitlimit(const ExpLoitlimit&) = delete;
            ExpLoitlimit& operator=(const ExpLoitlimit&) = delete;
            ExpLoitlimit(ExpLoitlimit&&) = default;
            ExpLoitlimit& operator=(ExpLoitlimit&&) = default;

            bool loadXml(const boost::property_tree::ptree& root);
            void dumpAll() const;
            void clear();

            uint32_t getDayLimit() const { return daylimit_; }
            uint32_t getWeekLimit() const { return weeklimit_; }
            uint32_t getDaySubLimit() const { return daysublimit_; }
            uint32_t getWeekSubLimit() const { return weeksublimit_; }

        private:
            uint32_t daylimit_ = 0;
            uint32_t weeklimit_ = 0;
            uint32_t daysublimit_ = 0;
            uint32_t weeksublimit_ = 0;
        };

        ExpLoitlimit exploitlimit;

        // --- TopList ---
        class TopList
        {
        public:
            TopList() = default;
            ~TopList() = default;
            TopList(const TopList&) = delete;
            TopList& operator=(const TopList&) = delete;
            TopList(TopList&&) = default;
            TopList& operator=(TopList&&) = default;

            bool loadXml(const boost::property_tree::ptree& root);
            void dumpAll() const;
            void clear();

            uint32_t getOpen() const { return open_; }
            uint32_t getRound() const { return round_; }
            uint32_t getMaxShow() const { return maxshow_; }
            std::string getFromTime() const { return fromtime_; }
            std::string getToTime() const { return totime_; }

        private:
            uint32_t open_ = 0;
            uint32_t round_ = 0;
            uint32_t maxshow_ = 0;
            std::string fromtime_ = "";
            std::string totime_ = "";
        };

        TopList toplist;

        // --- AppointOfficial ---
        class AppointOfficial
        {
        public:
            AppointOfficial() = default;
            ~AppointOfficial() = default;
            AppointOfficial(const AppointOfficial&) = delete;
            AppointOfficial& operator=(const AppointOfficial&) = delete;
            AppointOfficial(AppointOfficial&&) = default;
            AppointOfficial& operator=(AppointOfficial&&) = default;

            bool loadXml(const boost::property_tree::ptree& root);
            void dumpAll() const;
            void clear();

            uint32_t getOpen() const { return open_; }
            uint32_t getMinOfficialId() const { return minofficialid_; }
            uint32_t getMinExpLoit() const { return minexploit_; }
            uint32_t getReappointcd() const { return reappointcd_; }
            uint32_t getAppointProtecttime() const { return appointprotecttime_; }

            // --- ExtraOfficial ---
            class ExtraOfficial
            {
            public:
                ExtraOfficial() = default;
                ~ExtraOfficial() = default;
                ExtraOfficial(const ExtraOfficial&) = delete;
                ExtraOfficial& operator=(const ExtraOfficial&) = delete;
                ExtraOfficial(ExtraOfficial&&) = default;
                ExtraOfficial& operator=(ExtraOfficial&&) = default;

                bool loadXml(const boost::property_tree::ptree& root);
                void dumpAll() const;
                void clear();

                uint32_t getOfficialId() const { return officialid_; }
                std::string getOfficialName() const { return officialname_; }
                uint32_t getMaxNum() const { return maxnum_; }

            private:
                uint32_t officialid_ = 0;
                std::string officialname_ = "";
                uint32_t maxnum_ = 0;
            };

            // <officialid, ExtraOfficial>
            std::map<uint32_t, ExtraOfficial> extraofficial;

        private:
            uint32_t open_ = 0;
            uint32_t minofficialid_ = 0;
            uint32_t minexploit_ = 0;
            uint32_t reappointcd_ = 0;
            uint32_t appointprotecttime_ = 0;
        };

        AppointOfficial appointofficial;

    private:
        std::string xml_file_path_ = "";
    };

} // namespace xml
