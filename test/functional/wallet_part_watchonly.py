#!/usr/bin/env python3
# Copyright (c) 2017-2020 The Particl Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_particl import GhostTestFramework, isclose


class WalletParticlWatchOnlyTest(GhostTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        self.extra_args = [ ['-debug','-reservebalance=10000000', '-anonrestricted=0'] for i in range(self.num_nodes)]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_network(self, split=False):
        self.add_nodes(self.num_nodes, extra_args=self.extra_args)
        self.start_nodes()

        self.connect_nodes_bi(0, 1)
        self.connect_nodes_bi(0, 2)

    def run_test(self):
        nodes = self.nodes

        self.import_genesis_coins_a(nodes[0])

        addr = 'pcwP4hTtaMb7n4urszBTsgxWLdNLU4yNGz'
        nodes[1].importaddress(addr, addr, True)

        ro = nodes[1].getaddressinfo(addr)
        assert (ro['ismine'] == False)
        assert (ro['iswatchonly'] == True)

        assert (isclose(nodes[1].getwalletinfo()['watchonly_balance'], 10000.0))
        assert (len(nodes[1].filtertransactions({'include_watchonly': True})) == 1)

        self.log.info('Import watchonly account')
        ro = nodes[2].extkey('importaccount', nodes[0].extkey('account', 'default', 'true')['epkey'])
        nodes[2].extkey('setdefaultaccount', ro['account_id'])

        w0 = nodes[0].getwalletinfo()
        w2 = nodes[2].getwalletinfo()

        assert (w0['total_balance'] == w2['watchonly_total_balance'])
        assert (w0['txcount'] == w2['txcount'])

        sxaddr0 = nodes[0].getnewstealthaddress()
        sxaddrs = nodes[0].liststealthaddresses(True)
        addr_info = nodes[0].getaddressinfo(sxaddr0)
        assert (addr_info['ismine'] is True)
        assert (addr_info['isstealthaddress'] is True)
        scan_vk = sxaddrs[0]['Stealth Addresses'][0]['Scan Secret']
        spend_pk = sxaddrs[0]['Stealth Addresses'][0]['spend_public_key']
        spend_vk = sxaddrs[0]['Stealth Addresses'][0]['Spend Secret']
        ro = nodes[2].importstealthaddress(scan_vk, spend_pk)
        assert (ro['stealth_address'] == sxaddr0)
        assert (ro['watchonly'] == True)
        ro = nodes[2].getaddressinfo(sxaddr0)
        assert (ro['ismine'] == False)
        assert (ro['iswatchonly'] == True)

        nodes[0].sendtoaddress(sxaddr0, 1.0)
        self.stakeBlocks(1)

        w0 = nodes[0].getwalletinfo()
        w2 = nodes[2].getwalletinfo()

        assert (w0['total_balance'] == w2['watchonly_total_balance'])
        assert (w0['txcount'] == w2['txcount'])


        self.log.info('Test sending blind output to watchonly')
        coincontrol = {'blind_watchonly_visible': True}
        outputs = [{'address': sxaddr0, 'amount': 10},]
        nodes[0].sendtypeto('part', 'blind', outputs, 'comment', 'comment-to', 4, 64, False, coincontrol)
        self.stakeBlocks(1)
        w0 = nodes[0].getbalances()
        w2 = nodes[2].getbalances()
        assert (isclose(w0['mine']['blind_trusted'], 10.0))
        assert ('watchonly' not in w0)
        assert (isclose(w2['mine']['blind_trusted'], 0.0))
        assert (isclose(w2['watchonly']['blind_trusted'], 10.0))

        self.log.info('Test sending anon output to watchonly')
        coincontrol = {'blind_watchonly_visible': True}
        outputs = [{'address': sxaddr0, 'amount': 10},]
        nodes[0].sendtypeto('part', 'anon', outputs, 'comment', 'comment-to', 4, 64, False, coincontrol)
        self.stakeBlocks(1)
        w0 = nodes[0].getbalances()
        w2 = nodes[2].getbalances()
        assert (isclose(w0['mine']['anon_immature'], 10.0))
        assert ('watchonly' not in w0)
        assert (isclose(w2['mine']['anon_immature'], 0.0))
        assert (isclose(w2['watchonly']['anon_immature'], 10.0))

        self.log.info('Test fully importing the watchonly stealth address')
        nodes[2].importstealthaddress(scan_vk, spend_vk)
        ro = nodes[2].getaddressinfo(sxaddr0)
        assert (ro['ismine'] == True)
        assert (ro['iswatchonly'] == False)

        nodes[2].rescanblockchain(0)

        w2_balances = nodes[2].getbalances()
        w2_bm = w2_balances['mine']
        assert (w2_bm['trusted'] == 1.0 or w2_bm['staked'] > 1.0)
        assert (isclose(w2_bm['blind_trusted'], 10.0))
        assert (isclose(w2_bm['anon_immature'], 10.0))
        w2_bw = w2_balances['watchonly']
        assert (w2_bw['anon_immature'] == 0.0)
        assert (w2_bw['anon_immature'] == 0.0)


if __name__ == '__main__':
    WalletParticlWatchOnlyTest().main()
