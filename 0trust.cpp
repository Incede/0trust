#include "0trust.hpp"

ACTION 0trust::makeorder(name maker_account,string target_token_contract_str,asset amount_of_token,uint8_t buy_or_sell,uint128_t price) {

  require_auth (maker_account);

  eosio_assert (target_token_contract_str.size() <= 12, "Invalid Token Contract");
  uint64_t target_token_contract = name(target_token_contract_str).value; //LNC -> uint64_t
  uint64_t eosio_token_contract = "eosio.token"_n.value;

  eosio_assert (buy_or_sell ==0 || buy_or_sell == 1, "Invalid buy or sell param");
  accounts maker_account_table (_self, maker_account.value); // .value() is a member of the name class which gives account name in uint64_t

  if (buy_or_sell == 1) {
    auto it = maker_account_table.find(eosio_token_contract);
    eosio_assert(it !=maker_account_table.end(), "No EOS deposited");
    uint128_t amount_of_eos = price * amount_of_token.amount;
    maker_account_table.modify (it, maker_account, [&](auto& s){
      eosio_assert(amount_of_eos <= s.balance.amount, "Insufficient token balance");
      s.balance.amount -= amount_of_eos;
    });
  } 
  
  else if (buy_or_sell == 0) {
    auto it = maker_account_table.find(target_token_contract);
    eosio_assert(it != maker_account_table.end(), "No tokens deposited");
    maker_account_table.modify(it, maker_account, [&](auto& s){
      eosio_assert(amount_of_token <= s.balance, "Insufficient token balance");
      s.balance -= amount_of_token; 
    });
  }
  
  counter total_orders(_self, target_token_contract);
  auto total_order_count_ref = total_orders.find(256);
  if (total_order_count_ref == total_orders.end()) {
    total_orders.emplace(maker_account, [&](auto& s){
      s.current_count++;  
    });
  } else {
    total_orders.modify(total_order_count_ref, maker_account, [&](auto& s){
      s.current_count++;  
    });
  }
  auto order_id_ref = total_orders.find(256);
  uint64_t order_id_count = order_id_ref->current_count;

  if (buy_or_sell == 1) {
  buyorders_t buy_table(_self, target_token_contract);
  buy_table.emplace(maker_account, [&](auto& s){
    s.order_id = order_id_count;
    s.maker_account = maker_account;
    s.target_token_contract = target_token_contract;
    s.amount_of_token = amount_of_token;
    s.price = price;
    //s.timestamp = time_point_sec(now());
  });
  }
  else{
  sellorders_t sell_table(_self, target_token_contract);
  sell_table.emplace(maker_account, [&](auto& s){
    s.order_id = order_id_count;
    s.maker_account = maker_account;
    s.target_token_contract = target_token_contract;
    s.amount_of_token = amount_of_token;
    s.price = price;
    //s.timestamp = time_point_sec(now());
  }); 
  }
}

ACTION 0trust::matchorder(name taker_account,string target_token_contract_str,asset amount_of_token,uint8_t buy_or_sell,uint128_t take_price,uint64_t order_id)
{
  require_auth (taker_account);
  eosio_assert(target_token_contract_str.size() <= 12, "Invalid token contract");
  uint64_t target_token_contract = name(target_token_contract_str).value;
  uint64_t eosio_token_contract = "eosio.token"_n.value;
  eostemplate eos_template_ref(_self, _self.value);
  auto eos_asset_struct = eos_template_ref.find(256);
  const asset eos_asset = eos_asset_struct->eos_asset;
  sellorders_t sell_table(_self, target_token_contract);
  buyorders_t buy_table(_self, target_token_contract);
  
  if (buy_or_sell == 1) {

  accounts taker_account_table (_self, taker_account.value);
  auto price_index = sell_table.get_index<"bysellprice"_n>();

  for (auto itr= price_index.begin(); itr!=price_index.end();){
    auto order_ref = itr; 
    eosio_assert(order_ref != price_index.end(), "order not found in the sell table");
    if(order_ref->price > take_price) break;

    int64_t sold_token = amount_of_token.amount <= order_ref->amount_of_token.amount ? amount_of_token.amount : order_ref->amount_of_token.amount;
    int64_t sold_eos = sold_token * order_ref->price;

    price_index.modify(order_ref, _self, [&](auto& t){
      t.amount_of_token.amount -= sold_token;
    });

    //match processing
    uint128_t amount_of_eos = sold_eos;
    const name maker_account = order_ref->maker_account;
    eosio_assert(amount_of_token.symbol == order_ref->amount_of_token.symbol, "Symbol mismatch");
    accounts maker_account_table (_self, maker_account.value);

    auto it = taker_account_table.find(eosio_token_contract);
    eosio_assert (it != taker_account_table.end(), "No EOS");
    taker_account_table.modify(it, taker_account, [&](auto& s) {
    eosio_assert(amount_of_eos <= s.balance.amount, "Insufficient EOS balance!");
    s.balance.amount -= amount_of_eos; });

    auto itb = taker_account_table.find(target_token_contract);
    if (itb==taker_account_table.end()) {
    taker_account_table.emplace (taker_account, [&](auto& s){
      s.balance.amount = sold_token;
      s.token_contract = target_token_contract; });
    } 
    else {
    taker_account_table.modify(itb, taker_account, [&](auto& s){
      s.balance.amount += sold_token; });
    }

    auto itc = maker_account_table.find(eosio_token_contract);
    if (itc == maker_account_table.end()) {
    maker_account_table.emplace (taker_account, [&](auto& s){
      s.balance = eos_asset;
      s.balance.amount += amount_of_eos;
      s.token_contract = eosio_token_contract; });
    } else {
    maker_account_table.modify (itc, taker_account, [&](auto& s){
      s.balance.amount += amount_of_eos ; });
    }

    amount_of_token.amount -= sold_token;
    //erase order from sell table if order is taken
    if (order_ref->amount_of_token.amount ==0){
      itr = price_index.erase(order_ref);
      // current order fully matched
      if (amount_of_token.amount == 0) {
        buy_table.erase(buy_table.find(order_id));
        return; 
      } 
    }
    // if maker sell order is partially filled
    else {
      buy_table.erase(buy_table.find(order_id));
      return;}  
  } // end for loop

    //current order not fully matched
    eosio_assert(amount_of_token.amount!=0,"wrong loop exit");
    buy_table.modify(buy_table.find(order_id), _self, [&](auto& s){
      //eosio_assert(s.amount_of_token == amount_of_token,"no matching order found");
    s.amount_of_token = amount_of_token;
    });  
    return;
    
  }


  else if (buy_or_sell == 0) {

  accounts taker_account_table (_self, taker_account.value);
  auto price_index = buy_table.get_index<"bybuyprice"_n>();

  for (auto itr= price_index.begin(); itr!=price_index.end();) {
    auto order_ref = itr;
    auto make_price = MAX - order_ref->price;
    eosio_assert(order_ref != price_index.end(), "order not found in the sell table");
    //eosio_assert (make_price >= take_price,"error sorting buy table");
    if(make_price < take_price) break;

    int64_t sold_token = amount_of_token.amount <= order_ref->amount_of_token.amount ? amount_of_token.amount : order_ref->amount_of_token.amount;
    int64_t sold_eos = sold_token * make_price;

    price_index.modify(itr, _self, [&](auto& t){
      t.amount_of_token.amount -= sold_token;
    });

    uint128_t amount_of_eos = sold_eos;
    const name maker_account = order_ref->maker_account;
    eosio_assert(amount_of_token.symbol == order_ref->amount_of_token.symbol, "Symbol mismatch");
    accounts maker_account_table (_self, maker_account.value);

    auto it = taker_account_table.find (target_token_contract);
    eosio_assert (it != taker_account_table.end(), "given asset not present");
    taker_account_table.modify (it, taker_account, [&](auto& s){ 
    eosio_assert(sold_token <= s.balance.amount, "Insufficient tokens"); 
    s.balance.amount -= sold_token; }) ;

    auto itb = taker_account_table.find (eosio_token_contract);
    if (itb == taker_account_table.end()) {
    taker_account_table.emplace (taker_account, [&](auto& s){
      s.balance = eos_asset;
      s.balance.amount = sold_eos;
      s.token_contract = eosio_token_contract; }) ; 
    }
    else { 
    taker_account_table.modify(itb, taker_account, [&](auto& s){
      s.balance.amount += sold_eos; }); 
    }

    auto itc = maker_account_table.find(target_token_contract);
    if(itc == maker_account_table.end()){
    maker_account_table.emplace(taker_account,[&](auto& s){
      s.balance.amount=sold_token; 
      s.token_contract=target_token_contract;});}
    else { 
    maker_account_table.modify(itc, taker_account, [&](auto& s) {
      s.balance.amount += sold_token; }); 
    }

    amount_of_token.amount -= sold_token;
    if (order_ref->amount_of_token.amount == 0) {
      itr = price_index.erase(itr);
      if (amount_of_token.amount == 0) {
        sell_table.erase(sell_table.find(order_id));
        return;
      }
    }
    else {
      sell_table.erase(sell_table.find(order_id));
      return;
    }

  }

    sell_table.modify(sell_table.find(order_id), _self, [&](auto& s){
      s.amount_of_token = amount_of_token;
    });
    return;

  }

}

ACTION 0trust::makewithdraw(name withdraw_account,string target_token_contract_str,asset amount_of_token)
{ 
  require_auth(withdraw_account);

  eosio_assert(target_token_contract_str.size() <= 12, "Invalid target_token_contract");
  uint64_t target_token_contract = name(target_token_contract_str).value;
  accounts withdraw_account_table(_self, withdraw_account.value);
  auto it = withdraw_account_table.find(target_token_contract);
  eosio_assert(it != withdraw_account_table.end(), "You do not have any available balances");
  
  asset current_balance = it->balance;
  eosio_assert(current_balance >= amount_of_token, "You do not have enough of the given asset");
  if (current_balance == amount_of_token) {
    withdraw_account_table.erase(it);
  } else {
    withdraw_account_table.modify(it, withdraw_account, [&](auto& s){
      s.balance -= amount_of_token;
    });
  }
  
  action(
        permission_level{_self, "active"_n},
        name(target_token_contract_str), "transfer"_n,
        make_tuple(_self, withdraw_account, amount_of_token,
            string("makewithdraw"))
    ).send();    
}

ACTION 0trust::cancelorder(string target_token_contract_str,uint8_t buy_or_sell,uint64_t order_id) {

  eosio_assert(target_token_contract_str.size() <= 12, "Invalid target_token_contract");
  uint64_t target_token_contract = name(target_token_contract_str).value;
  uint64_t eosio_token_contract = "eosio.token"_n.value;

  if (buy_or_sell == 1) {
    buyorders_t order_table(_self, target_token_contract);
    auto order_ref = order_table.find(order_id);
    eosio_assert(order_ref != order_table.end(), "Could not find the specified order");
    name maker_account = order_ref->maker_account;
    require_auth(maker_account);
    accounts account_table(_self, maker_account.value);

    auto it = account_table.find(eosio_token_contract);
    uint128_t amount_of_eos = order_ref->amount_of_token.amount * order_ref->price;
    account_table.modify(it, maker_account, [&](auto& s){
      s.balance.amount += amount_of_eos;
    });
    order_table.erase(order_ref);

  } else if (buy_or_sell == 0) {
    sellorders_t order_table(_self, target_token_contract);
    auto order_ref = order_table.find(order_id);
    eosio_assert(order_ref != order_table.end(), "Could not find the specified order");
    name maker_account = order_ref->maker_account;
    require_auth(maker_account);
    accounts account_table(_self, maker_account.value);

    auto it = account_table.find(target_token_contract);
    account_table.modify(it, maker_account, [&](auto& s){
      s.balance += order_ref->amount_of_token;
    });

    order_table.erase(order_ref);
  }
}

ACTION 0trust::seteosasset(asset eos_asset) {
  require_auth(_self);
  eostemplate eos_template(_self, _self.value);
  eos_template.emplace(_self, [&](auto& s){
    s.eos_asset = eos_asset;
  });
}

ACTION 0trust::setadmin(name admin_acc) {
  admintable admin_table(_self,_self.value);
  auto it = admin_table.find(256);
  if (it != admin_table.end()) {
    admin_table.modify(it, admin_acc, [&](auto& s){
      s.admin = admin_acc;
    });
  } else {
    admin_table.emplace(admin_acc, [&](auto& s) {
      s.admin = admin_acc;
    });
  }
}

void 0trust::deposit(name from, name to, asset quantity, string memo) {
  if(from == _self || to != _self)
    return;

  accounts st(_self, from.value);
  auto it = st.find(_code.value);
  if (it != st.end()) {
    st.modify(it, from, [&](auto& s) {
        s.balance += quantity;
      }
    );
  } else {
    st.emplace(_self, [&](auto& s) {
        s.token_contract = _code.value;
        s.balance = quantity;
      }
    );
  }
}

