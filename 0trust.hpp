#include <eosiolib/eosio.hpp>
#include <eosiolib/print.hpp>
#include <eosiolib/asset.hpp>
#include <eosiolib/multi_index.hpp>
#include <eosiolib/action.hpp>
#include <eosiolib/time.hpp>
#include <string>

using namespace eosio; 
using std::string; 
const auto EOS_SYMBOL = eosio::symbol("EOS", 4); //EOS token , 4= precision
const auto TOKEN_CONTRACT = "eosio.token"_n;    // EOS contract address
constexpr auto EOS_CONTRACT = "eosio.token"_n;  // same as above
const uint64_t MAX = 10000;                     // for sorting the buy table in a reverse way
constexpr uint64_t PRICE_SCALE = 100000000;     // 

CONTRACT 0trust : public contract {

public:
  using contract::contract;

  ACTION makeorder(name maker_account,string target_token_contract_str,asset amount_of_token,uint8_t buy_or_sell,uint128_t price );  
  ACTION matchorder(name taker_account,string target_token_contract_str,asset amount_of_token,uint8_t buy_or_sell,uint128_t take_price,uint64_t order_id);
  ACTION makewithdraw(name withdraw_account,string target_token_contract_str,asset amount_of_token);
  ACTION cancelorder(string target_token_contract_str,uint8_t buy_or_sell,uint64_t order_id);
  ACTION seteosasset(asset eos_asset);
  ACTION setadmin(name administrator);

  // struct definition 
  TABLE admin {
    uint64_t arbritrary_key = 256;
    name admin;   
    uint64_t primary_key() const { return arbritrary_key; }
  };

  typedef multi_index< "admintable"_n, admin > admintable;

  TABLE eos_asseta {
    uint64_t arbritrary_key = 256; 
    asset eos_asset;    
    uint64_t primary_key() const { return arbritrary_key; }
  };

  TABLE balances {
    uint64_t token_contract;
    asset balance;
    uint64_t primary_key() const { return token_contract; }
  };

  TABLE sellorders {
    uint64_t order_id;
    name maker_account;
    uint64_t target_token_contract;
        uint64_t base_token_contract;
    asset amount_of_token;
    uint128_t price;
    //time_point_sec timestamp;
    uint64_t primary_key() const { return order_id; }
    uint128_t get_price() const {return price;}
    //time_point_sec get_time() const {return timestamp;}
  };



  TABLE buyorders {
    uint64_t order_id; //435346
    name maker_account; 
    uint64_t target_token_contract; // linkercoinkr
        uint64_t base_token_contract;
    asset amount_of_token;   // 0.4000 LNC
    uint128_t price;         // 1.2000 EOS
    time_point_sec timestamp;  // now()
    uint64_t primary_key() const { return order_id; }  // primary key O log(n)
    uint128_t get_price() const {return MAX-price;}  //secondary key 
    time_point_sec get_timestamp() const {return timestamp;}
  };

  typedef multi_index < "buyorders"_n,buyorders,indexed_by<"bybuyprice"_n,const_mem_fun<buyorders,uint128_t,&buyorders::get_price>>> buyorders_t;

  // 1. struct - buyorders
  // 2. secondary key search function specification - const_mem_fun< get_price>
  // 3


  TABLE count {
    uint64_t current_count = 0;
    uint64_t arbritrary_key = 256; 
    uint64_t primary_key() const { return arbritrary_key; }
  };

  typedef multi_index< "eostemplate"_n, eos_asseta > eostemplate;
    
    
  //indexed_by<"bytimestamp"_n,const_mem_fun<buyorders,time_point_sec,&buyorders::get_timestamp>> > buyorders_t;
  typedef multi_index< "sellorders"_n,sellorders,indexed_by<"bysellprice"_n,const_mem_fun<sellorders,uint128_t,&sellorders::get_price>>> sellorders_t;
    typedef multi_index< "accounts"_n, balances > accounts;
  typedef multi_index< "counter"_n, count > counter;

  void deposit(name from, name to, asset quantity, string memo);
    
  // standard EOS signature for creating the ABI
  void apply(uint64_t receiver, uint64_t code, uint64_t action);
};

struct st_transfer {
    name from;
    name to;
    asset quantity;
    string memo;
    EOSLIB_SERIALIZE( st_transfer, (from)(to)(quantity)(memo) )
};

// standard EOS signature for creating the ABI
void 0trust::apply(uint64_t receiver, uint64_t code, uint64_t action) {
    auto &thiscontract = *this;
    if (action == ( "transfer"_n ).value ) {
        auto transfer_data = unpack_action_data<st_transfer>();
        if (transfer_data.quantity.symbol == EOS_SYMBOL)
            eosio_assert(name(code) == TOKEN_CONTRACT, "Transfer EOS must go through eosio.token...");
        
        deposit(transfer_data.from, transfer_data.to, transfer_data.quantity, transfer_data.memo);
        return;
    }

    if (code != get_self().value) return;

    switch (action) {
        EOSIO_DISPATCH_HELPER(0trust,
            (makeorder)
            (matchorder)
            (makewithdraw)
            (cancelorder)
            (seteosasset)
            (setadmin)
        )
    }
}

// standard EOS signature for creating the ABI
extern "C" {
    [[noreturn]] void apply(uint64_t receiver, uint64_t code, uint64_t action) {
        0trust p( name(receiver), name(code), datastream<const char*>(nullptr, 0) );
        p.apply(receiver, code, action);
        eosio_exit(0);
    }
}
