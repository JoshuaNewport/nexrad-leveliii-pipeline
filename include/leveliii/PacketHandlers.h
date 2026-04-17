#pragma once

#include "LevelIII_Types.h"
#include <cstdint>
#include <iostream>
#include <memory>

namespace leveliii {

struct PacketContext {
    int16_t product_code;
    const ProductDescriptionBlock& pdb;
};

class PacketHandler {
public:
    virtual ~PacketHandler() = default;
    virtual bool CanHandle(uint16_t packet_code) const = 0;
    virtual bool Parse(std::istream& is, const PacketContext& ctx, RadarFrame& frame) = 0;
};

class RadialPacketHandler : public PacketHandler {
public:
    bool CanHandle(uint16_t packet_code) const override;
    bool Parse(std::istream& is, const PacketContext& ctx, RadarFrame& frame) override;
};

class RleRadialPacketHandler : public PacketHandler {
public:
    bool CanHandle(uint16_t packet_code) const override;
    bool Parse(std::istream& is, const PacketContext& ctx, RadarFrame& frame) override;
};

class RasterPacketHandler : public PacketHandler {
public:
    bool CanHandle(uint16_t packet_code) const override;
    bool Parse(std::istream& is, const PacketContext& ctx, RadarFrame& frame) override;
};

class RasterDataPacketHandler : public PacketHandler {
public:
    bool CanHandle(uint16_t packet_code) const override;
    bool Parse(std::istream& is, const PacketContext& ctx, RadarFrame& frame) override;
};

class GenericPacketHandler : public PacketHandler {
public:
    bool CanHandle(uint16_t packet_code) const override;
    bool Parse(std::istream& is, const PacketContext& ctx, RadarFrame& frame) override;
};

class TextSymbolPacketHandler : public PacketHandler {
public:
    bool CanHandle(uint16_t packet_code) const override;
    bool Parse(std::istream& is, const PacketContext& ctx, RadarFrame& frame) override;
};

class ContourPacketHandler : public PacketHandler {
public:
    bool CanHandle(uint16_t packet_code) const override;
    bool Parse(std::istream& is, const PacketContext& ctx, RadarFrame& frame) override;
};

class GenericBytePacketHandler : public PacketHandler {
public:
    bool CanHandle(uint16_t packet_code) const override;
    bool Parse(std::istream& is, const PacketContext& ctx, RadarFrame& frame) override;
};

class PacketHandlerRegistry {
public:
    static PacketHandlerRegistry& Instance();
    
    std::shared_ptr<PacketHandler> GetHandler(uint16_t packet_code);
    bool ParsePacket(std::istream& is, uint16_t packet_code, 
                    const PacketContext& ctx, RadarFrame& frame);

private:
    PacketHandlerRegistry();
    std::vector<std::shared_ptr<PacketHandler>> handlers_;
};

} // namespace leveliii
